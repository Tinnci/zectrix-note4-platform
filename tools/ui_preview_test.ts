import { expect, test } from "bun:test";
import { obsoletePreviewFiles, pbmToSvg } from "./ui-preview";

test("retire only obsolete managed scenes, not user images or unsafe manifest paths", () => {
  const previous = [{name:"old-screen"}, {name:"current-screen"}, {name:"../private"}, null];
  expect(obsoletePreviewFiles(["old-screen.pbm", "old-screen.svg", "old-screen.text.json", "current-screen.pbm", "current-screen.svg", "current-screen.text.json", "my-photo.png", "overview.png"],
    new Set(["current-screen.pbm", "current-screen.svg", "current-screen.text.json"]), previous)).toEqual(["old-screen.pbm", "old-screen.svg", "old-screen.text.json", "overview.png"]);
  expect(obsoletePreviewFiles(["my-photo.png"], new Set(), {})).toEqual([]);
});

test("native preview preserves portrait row padding and edge pixels", () => {
  for (const [width, height] of [[300, 400], [400, 300]] as const) {
    const header = new TextEncoder().encode(`P4\n${width} ${height}\n`), stride = Math.ceil(width/8);
    const pbm = new Uint8Array(header.length+stride*height); pbm.set(header);
    pbm[header.length] = 0x80;
    pbm[header.length+(height-1)*stride+((width-1)>>3)] = 0x80>>((width-1)&7);
    const result = pbmToSvg(pbm);
    expect(result.width).toBe(width); expect(result.height).toBe(height);
    expect(result.svg).toContain("M0 0h1v1H0z");
    expect(result.svg).toContain(`M${width-1} ${height-1}h1v1H${width-1}z`);
    expect(() => pbmToSvg(pbm.subarray(0,pbm.length-1))).toThrow();
  }
  expect(() => pbmToSvg(new TextEncoder().encode("P4\n200 200\n"))).toThrow();
});
