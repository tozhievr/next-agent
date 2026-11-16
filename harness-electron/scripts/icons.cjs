const fs = require("node:fs");
const path = require("node:path");
const sharp = require("sharp");
(async () => {
  const root = path.resolve(__dirname, "..");
  const svg = fs.readFileSync(path.join(root, "public/brand.svg"));
  const dir = path.join(root, "assets");
  fs.mkdirSync(dir, { recursive: true });
  await sharp(svg).resize(1024).png().toFile(path.join(dir, "icon.png"));
  const sizes = [16, 24, 32, 48, 64, 128, 256];
  const images = await Promise.all(
    sizes.map((n) => sharp(svg).resize(n).png().toBuffer()),
  );
  const header = Buffer.alloc(6 + 16 * sizes.length);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(sizes.length, 4);
  let offset = header.length;
  images.forEach((data, i) => {
    const p = 6 + i * 16;
    header[p] = sizes[i] === 256 ? 0 : sizes[i];
    header[p + 1] = header[p];
    header.writeUInt16LE(1, p + 4);
    header.writeUInt16LE(32, p + 6);
    header.writeUInt32LE(data.length, p + 8);
    header.writeUInt32LE(offset, p + 12);
    offset += data.length;
  });
  const ico = Buffer.concat([header, ...images]);
  fs.writeFileSync(path.join(dir, "icon.ico"), ico);
  fs.writeFileSync(path.join(root, "app/favicon.ico"), ico);
  console.log("Generated PNG and multi-resolution ICO from brand.svg");
})().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
