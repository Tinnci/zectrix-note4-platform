import { connect, type MqttClient } from "mqtt";
import { Database } from "bun:sqlite";
import { createPageHandler } from "../edge-page-server";
import { deviceState, discovery, readStates, revisionCounter } from "./core";
import { renderStates } from "./renderer";

export async function mqttReport(client: MqttClient, id: string, request: Request, page: Uint8Array) {
  if (!client.connected) throw new Error("MQTT unavailable");
  const sample=deviceState(request,page);
  const messages = [...discovery(id,sample.interval_seconds*2+60), { topic: `note4/${id}/state`, payload: JSON.stringify(sample) }];
  // One publish in flight; no offline queue or reconnect replay of stale samples.
  for (const message of messages) await new Promise<void>((resolve,reject) => {
    const timer=setTimeout(()=>{ client.end(true); reject(new Error("MQTT publish timeout")); },400);
    client.publish(message.topic,message.payload,{retain:true,qos:1},error=>{clearTimeout(timer); error ? reject(new Error("MQTT publish failed")) : resolve();});
  });
}

if (import.meta.main) {
  const { HA_URL, HA_TOKEN, HA_ENTITIES, EDGE_TOKEN, MQTT_URL, NOTE4_DEVICE_ID }=Bun.env;
  if (!HA_URL || !HA_TOKEN || !HA_ENTITIES || !EDGE_TOKEN || !MQTT_URL || !NOTE4_DEVICE_ID) throw new Error("Set HA_URL, HA_TOKEN, HA_ENTITIES, EDGE_TOKEN, MQTT_URL and NOTE4_DEVICE_ID");
  const orientation = Bun.env.EDGE_ORIENTATION ?? "portrait";
  if (orientation !== "portrait" && orientation !== "landscape") throw new Error("Invalid EDGE_ORIENTATION");
  const broker=new URL(MQTT_URL);
  if ((broker.protocol!=="mqtts:" && !(broker.protocol==="mqtt:" && Bun.env.MQTT_ALLOW_INSECURE==="1")) || broker.username || broker.password || broker.search || broker.hash) throw new Error("Use MQTT TLS, or explicitly allow LAN MQTT; credentials belong in environment variables");
  discovery(NOTE4_DEVICE_ID); // Validate before any connection.
  const newClient=()=>{
    const c=connect(MQTT_URL,{username:Bun.env.MQTT_USERNAME,password:Bun.env.MQTT_PASSWORD,
      // MQTT.js packet debug can include CONNECT credentials. Keep only our
      // credential-free event logs, even when DEBUG is set externally.
      log:()=>{},connectTimeout:3000,reconnectPeriod:10000,queueQoSZero:false,clean:true,clientId:`note4_bridge_${NOTE4_DEVICE_ID}`});
    c.on("error",()=>console.warn("event=mqtt_unavailable")); return c;
  };
  let client=newClient();
  const font=new Uint8Array(await Bun.file(`${import.meta.dir}/../../components/note4_reader/font/reader_font.bin`).arrayBuffer());
  const db=new Database(Bun.env.EDGE_DB_PATH ?? `${import.meta.dir}/bridge.sqlite`,{create:true});
  const nextRevision=revisionCounter(db), entities=HA_ENTITIES.split(",");
  const handler=createPageHandler(async()=>renderStates(font,await readStates(HA_URL,HA_TOKEN,entities,Bun.env.HA_ALLOW_HTTP==="1"),`${new Date().toISOString().slice(0,16)} UTC`,orientation),EDGE_TOKEN,
    ()=>Math.floor(Date.now()/1000),async(request,page)=>{
      try {await mqttReport(client,NOTE4_DEVICE_ID,request,page);}
      catch {
        console.warn("event=mqtt_report_failed");
        // A timeout destroys the old offline queue; a fresh client recovers
        // without replaying old battery readings as current samples.
        if (client.disconnecting) client=newClient();
      }
    },nextRevision);
  const port=Number(Bun.env.EDGE_PORT ?? "8787");
  if (!Number.isInteger(port)||port<1||port>65535) throw new Error("Invalid EDGE_PORT");
  const server=Bun.serve({hostname:"127.0.0.1",port,fetch:handler});
  console.log(`HA bridge listening on 127.0.0.1:${server.port}; HTTPS proxy required`);
  const stop=async()=>{await server.stop(true); await client.endAsync(true); db.close(); process.exit(0);};
  process.on("SIGINT",stop); process.on("SIGTERM",stop);
}
