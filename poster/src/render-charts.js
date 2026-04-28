(() => {
const data = window.posterData;

const chartSpecs = [
  {
    id: "single-chart",
    title: "Single-Core IPC Gain",
    caption: "4/4 reported workloads improve.",
    data: data.singleCore,
    max: 4.5
  },
  {
    id: "hetero-chart",
    title: "Two-Core Heterogeneous WS Gain",
    caption: "6/6 mixed pairs improve.",
    data: data.heteroPairs,
    max: 4.5
  },
  {
    id: "homo-chart",
    title: "Two-Core Homogeneous WS Gain",
    caption: "4/4 same-workload pairs improve.",
    data: data.homoPairs,
    max: 4.5
  }
];

const svgNS = "http://www.w3.org/2000/svg";

function pct(value) {
  return `${value >= 0 ? "+" : ""}${value.toFixed(2)}%`;
}

function axisPct(value) {
  if (value === 0) return "0%";
  if (value === 2.25) return "2.25%";
  if (value === 4.5) return "4.5%";
  return `${value}%`;
}

function node(name, attrs = {}, text = "") {
  const el = document.createElementNS(svgNS, name);
  Object.entries(attrs).forEach(([key, value]) => el.setAttribute(key, value));
  if (text) el.textContent = text;
  return el;
}

function renderBarChart({ id, title, caption, data, max }) {
  const host = document.getElementById(id);
  if (!host) return;

  const sorted = [...data].sort((a, b) => b.gain - a.gain);
  const width = 900;
  const rowHeight = 64;
  const top = 112;
  const left = 260;
  const right = 128;
  const barHeight = 34;
  const chartWidth = width - left - right;
  const height = top + sorted.length * rowHeight + 94;

  const svg = node("svg", {
    viewBox: `0 0 ${width} ${height}`,
    role: "img",
    "aria-label": `${title} bar chart`
  });

  svg.append(node("text", { x: 0, y: 36, class: "chart-title" }, title));

  svg.append(node("line", {
    x1: left,
    x2: left + chartWidth,
    y1: 88,
    y2: 88,
    class: "axis-line"
  }));

  [0, 2.25, 4.5].forEach((tick) => {
    const x = left + (tick / max) * chartWidth;
    svg.append(node("line", { x1: x, x2: x, y1: 82, y2: 94, class: "tick-line" }));
    svg.append(node("text", { x, y: 72, class: "tick-label", "text-anchor": "middle" }, axisPct(tick)));
  });

  sorted.forEach((entry, i) => {
    const y = top + i * rowHeight;
    const barWidth = Math.max(3, (entry.gain / max) * chartWidth);
    const colorClass = entry.label === "astar + astar" ? "bar-accent" : "bar-main";

    svg.append(node("text", { x: 0, y: y + 28, class: "bar-label" }, entry.label));
    svg.append(node("rect", {
      x: left,
      y: y,
      width: chartWidth,
      height: barHeight,
      rx: 6,
      class: "bar-track"
    }));
    svg.append(node("rect", {
      x: left,
      y,
      width: barWidth,
      height: barHeight,
      rx: 6,
      class: colorClass
    }));
    svg.append(node("text", {
      x: left + barWidth + 18,
      y: y + 26,
      class: "bar-value"
    }, pct(entry.gain)));
  });

  svg.append(node("text", { x: 0, y: height - 24, class: "chart-caption" }, caption));
  host.replaceChildren(svg);
}

window.addEventListener("DOMContentLoaded", () => {
  chartSpecs.forEach(renderBarChart);
  document.documentElement.dataset.chartsReady = "true";
});
})();
