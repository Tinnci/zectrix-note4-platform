import type { HAState } from "./core";
// Reuse the repository's OFL GNU Unifont subset; rendering stays on the server.
const ranges = [[0x20,0x2ff],[0x2000,0x206f],[0x3000,0x30ff],[0x31f0,0x31ff],[0x3400,0x9fff],[0xac00,0xd7a3],[0xff00,0xffef],[0xfffd,0xfffd]] as const;
const count = ranges.reduce((n, [a,b]) => n+b-a+1, 0), widthBytes = Math.ceil(count / 8), tileBase = widthBytes + count*8;
export type PageOrientation = "portrait" | "landscape";
export function renderStates(font: Uint8Array, states: HAState[], date: string, orientation: PageOrientation = "portrait"): Uint8Array {
  if (orientation !== "portrait" && orientation !== "landscape") throw new Error("Invalid page orientation");
  if (font.length < tileBase || (font.length - tileBase) % 8 || states.length > 8) throw new Error("Invalid renderer input");
  const width = orientation === "portrait" ? 300 : 400, height = orientation === "portrait" ? 400 : 300;
  const stride = Math.ceil(width / 8);
  const header = new TextEncoder().encode(`P4\n${width} ${height}\n`), frame = new Uint8Array(header.length + stride*height); frame.set(header);
  const pixel = (x: number, y: number) => { if (x>=0 && x<width && y>=0 && y<height) frame[header.length+y*stride+(x>>3)]! |= 0x80>>(x&7); };
  const index = (code: number) => { let base=0; for (const [a,b] of ranges) { if (code>=a && code<=b) return base+code-a; base+=b-a+1; } return count-1; };
  const text = (value: string, left: number, y: number, right: number, scale=1) => { let x=left; for (const char of [...value].slice(0,64)) {
    const i=index(char.codePointAt(0)!), width=(font[i>>3]! & (1<<(i&7))) ? 16 : 8;
    if (x+width*scale>right) break;
    for (let tile=0;tile<4;tile++) { const p=widthBytes+i*8+tile*2, offset=tileBase+(font[p]! | font[p+1]!<<8)*8;
      if (offset+8>font.length) throw new Error("Invalid font tile");
      for (let row=0;row<8;row++) for(let col=0;col<8;col++) if(font[offset+row]! & (0x80>>col))
        for(let dy=0;dy<scale;dy++) for(let dx=0;dx<scale;dx++) pixel(x+((tile%2)*8+col)*scale+dx,y+(Math.floor(tile/2)*8+row)*scale+dy);
    } x+=width*scale;
  } };
  text("HOME ASSISTANT", 12, 10, width-12, 2); text(date, 12, 46, width-12);
  // Landscape is reflowed into two columns, never a rotated portrait bitmap.
  const columns = orientation === "landscape" ? 2 : 1;
  const columnWidth = (width-24) / columns;
  states.forEach((state,i) => { const column = i % columns, row = Math.floor(i / columns);
    const left = 12 + column * columnWidth, right = left + columnWidth - 12;
    const y=76+row*(columns === 2 ? 54 : 39);
    text(state.attributes.friendly_name ?? state.entity_id,left,y,right);
    text(`${state.state} ${state.attributes.unit_of_measurement ?? ""}`,left,y+18,right);
    for(let x=left;x<right;x++) pixel(x,y+36);
  });
  return frame;
}
