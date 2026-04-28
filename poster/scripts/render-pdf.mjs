import { chromium } from "playwright";
import { mkdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const src = `file://${path.join(root, "src", "index.html")}`;
const outDir = path.join(root, "dist");
const outFile = path.join(outDir, "bwc-adaptive-selector-poster.pdf");

await mkdir(outDir, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 4800, height: 3600 }, deviceScaleFactor: 1 });
await page.emulateMedia({ media: "screen" });
await page.goto(src, { waitUntil: "networkidle" });
await page.waitForFunction(() => document.documentElement.dataset.chartsReady === "true");
await page.pdf({
  path: outFile,
  width: "48in",
  height: "36in",
  printBackground: true,
  preferCSSPageSize: true,
  margin: { top: "0in", right: "0in", bottom: "0in", left: "0in" }
});
await browser.close();

console.log(`Wrote ${outFile}`);
