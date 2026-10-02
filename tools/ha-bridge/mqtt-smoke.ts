// Explicit live check: publishes only to a random non-retained test topic.
import { connectAsync } from "mqtt";
const url=Bun.env.MQTT_TEST_URL;
if(!url) throw new Error("Set MQTT_TEST_URL for the live broker check");
const client=await connectAsync(url,{connectTimeout:3000,reconnectPeriod:0,clean:true,queueQoSZero:false});
const topic=`zectrix/note4/test/${crypto.randomUUID()}`;
try {
  await client.subscribeAsync(topic,{qos:1});
  const received=new Promise<void>((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error("MQTT roundtrip timeout")),3000);
    client.on("message",(name,payload)=>{if(name===topic&&payload.toString()==="probe"){clearTimeout(timer);resolve();}});
  });
  await client.publishAsync(topic,"probe",{qos:1,retain:false}); await received;
  console.log("PASS: live MQTT subscribe/publish QoS1 roundtrip (no retained data).");
} finally {await client.endAsync(true);}
