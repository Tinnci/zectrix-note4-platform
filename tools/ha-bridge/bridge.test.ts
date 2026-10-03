import { expect, test } from "bun:test";
import { Database } from "bun:sqlite";
import { deviceState, discovery, readStates, revisionCounter } from "./core";
import { renderStates } from "./renderer";
import { mqttReport } from "./server";
import { createPageHandler, encodePage } from "../edge-page-server";
import type { MqttClient } from "mqtt";

test("HA REST authentication, bounded state parsing and no credential exposure", async()=>{
  let mode="ok";
  const server=Bun.serve({hostname:"127.0.0.1",port:0,fetch:r=>{
    expect(r.headers.get("Authorization")).toBe("Bearer private-ha-token");
    if(mode==="auth") return new Response("private upstream body",{status:401});
    if(mode==="large") return new Response("x".repeat(17000));
    if(mode==="redirect") return Response.redirect("http://127.0.0.1:1/");
    return Response.json({entity_id:"sensor.room",state:"22",attributes:{friendly_name:"客厅",unit_of_measurement:"°C"}});
  }});
  const url=`http://127.0.0.1:${server.port}`;
  try {
    await expect(readStates(url,"private-ha-token",["sensor.room"])).rejects.toThrow("Invalid HA origin");
    expect((await readStates(url,"private-ha-token",["sensor.room"],true))[0]!.attributes.friendly_name).toBe("客厅");
    for(const value of ["auth","large","redirect"]) { mode=value; await expect(readStates(url,"private-ha-token",["sensor.room"],true)).rejects.toThrow(); }
    await expect(readStates(url,"token",["sensor.room?token=secret"],true)).rejects.toThrow("Invalid HA source");
  } finally {await server.stop(true);}
});

test("Unicode server rendering uses the existing font and produces a valid portrait page",async()=>{
  const font=new Uint8Array(await Bun.file(`${import.meta.dir}/../../components/note4_reader/font/reader_font.bin`).arrayBuffer());
  const pbm=renderStates(font,[{entity_id:"sensor.room",state:"22",attributes:{friendly_name:"客厅",unit_of_measurement:"°C"}}],"2026-10-03 12:00 UTC");
  expect(pbm.length).toBe(15211); expect(pbm.some((v,i)=>i>11&&v!==0)).toBe(true);
  const page=encodePage(pbm,1,1704067200,1704153600,0);
  expect(page.length).toBe(15032); expect(page[4]).toBe(1);
  expect(()=>renderStates(new Uint8Array(1),[],"")).toThrow();
});

test("persistent revision allocation survives restart and wall-clock rollback",()=>{
  const db=new Database(":memory:");
  const next=revisionCounter(db); expect(next(100)).toBe(100); expect(next(100)).toBe(101);
  expect(revisionCounter(db)(50)).toBe(102);
  expect(()=>next(0x100000000)).toThrow(); db.close();
});

test("HA layout reflows all eight entities in both native orientations", async () => {
  const font = new Uint8Array(await Bun.file(`${import.meta.dir}/../../components/note4_reader/font/reader_font.bin`).arrayBuffer());
  const states = Array.from({length: 8}, (_, i) => ({entity_id: `sensor.room_${i}`, state: String(i), attributes: {friendly_name: `房间 ${i}`}}));
  for (const orientation of ["portrait", "landscape"] as const) {
    const pbm = renderStates(font, states, "2026-10-03 12:00 UTC", orientation);
    expect(pbm.length).toBe(orientation === "portrait" ? 15211 : 15011);
    expect(encodePage(pbm,1,1704067200,1704153600,0)[4]).toBe(orientation === "portrait" ? 1 : 0);
    const width = orientation === "portrait" ? 300 : 400, stride = Math.ceil(width/8);
    const lastRow = orientation === "portrait" ? 349 : 238;
    expect(pbm.subarray(11+lastRow*stride,11+(lastRow+16)*stride).some(v=>v!==0)).toBe(true);
  }
  expect(() => renderStates(font, [...states,states[0]!], "")).toThrow();
});

test("discovery separates offered page from displayed page, and rejects stale retained state",()=>{
  const entries=discovery("test"); expect(entries).toHaveLength(4);
  const battery=JSON.parse(entries[0]!.payload);
  expect(battery.device.identifiers).toEqual(["note4_test"]);
  expect(entries[0]!.topic).toBe("homeassistant/sensor/note4_test/battery/config");
  expect(battery.unique_id).toBe("note4_test_battery");
  expect(battery.state_topic).toBe("note4/test/state");
  expect(battery.device_class).toBe("battery"); expect(battery.expire_after).toBe(43260);
  expect(battery.availability[0].value_template).toContain("sampled_at");
  expect(entries[3]!.topic).toContain("offered_revision");
  expect(()=>discovery("test/#")).toThrow();
});

test("authenticated page GET carries validated telemetry into retained MQTT publications",async()=>{
  const published:{topic:string,payload:string}[]=[];
  const client={connected:true,publish:(topic:string,payload:string,_options:unknown,done:()=>void)=>{published.push({topic,payload});done();}} as unknown as MqttClient;
  const font=new Uint8Array(await Bun.file(`${import.meta.dir}/../../components/note4_reader/font/reader_font.bin`).arrayBuffer());
  const token="0123456789abcdef0123456789abcdef";
  const handler=createPageHandler(async()=>renderStates(font,[],""),token,()=>1704067200,(r,p)=>mqttReport(client,"test",r,p));
  expect((await handler(new Request("https://display.example/note4/page"))).status).toBe(401); expect(published).toHaveLength(0);
  const request=new Request("https://display.example/note4/page",{headers:{Authorization:`Bearer ${token}`,"X-Note4-Battery":"80","X-Note4-Millivolts":"3700","X-Note4-Charging":"1","X-Note4-Interval":"86400"}});
  expect((await handler(request)).status).toBe(200); expect(published).toHaveLength(5);
  expect(published[4]!.topic).toBe("note4/test/state");
  const state=JSON.parse(published[4]!.payload); expect(state.battery).toBe(80);expect(state.voltage).toBe(3.7);expect(state.charging).toBe(true);
  expect(JSON.parse(published[0]!.payload).expire_after).toBe(172860);
  expect(JSON.stringify(published)).not.toContain(token);
  const page=new Uint8Array(15032); const invalid=deviceState(new Request("https://display.example",{headers:{"X-Note4-Battery":"101","X-Note4-Charging":"unknown"}}),page);
  expect(invalid.battery).toBeNull();expect(invalid.charging).toBeNull();
  const disconnected={connected:false} as MqttClient;
  const fallback=createPageHandler(async()=>renderStates(font,[],""),token,()=>1704067200,(r,p)=>mqttReport(disconnected,"test",r,p));
  expect((await fallback(request)).status).toBe(200);
});
