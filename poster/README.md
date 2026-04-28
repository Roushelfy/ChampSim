# BWC Adaptive Selector Poster

This directory contains a reproducible academic poster for the ChampSim course project:

**BWC Adaptive Selector: Pair-Aware Runtime Prefetcher Selection in ChampSim**

The poster is a single-page, 48in x 36in landscape PDF generated from offline HTML, CSS, JavaScript, and SVG. It is designed to be visual, chart-heavy, and readable during a short poster-session explanation.

## Install

```bash
npm install
npx playwright install chromium
```

## Generate

```bash
npm run build
```

## Outputs

```text
dist/bwc-adaptive-selector-poster.pdf
dist/bwc-adaptive-selector-poster.png
```

## Edit Content

- Text and poster structure: `src/index.html`
- Chart numbers: `src/data.js`
- Layout and visual style: `src/styles.css`
- Chart rendering: `src/render-charts.js`

## Print Note

The intended poster size is **48in x 36in landscape**. Before printing, verify the PDF page size in your PDF viewer or print dialog and confirm it remains a single page.
