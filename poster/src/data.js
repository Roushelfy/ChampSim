const singleCore = [
  { label: "astar", gain: 1.2498 },
  { label: "mcf", gain: 2.6685 },
  { label: "lbm", gain: 1.9456 },
  { label: "bzip2", gain: 0.8569 }
];

const heteroPairs = [
  { label: "mcf + astar", gain: 2.6552 },
  { label: "astar + bzip2", gain: 1.5144 },
  { label: "bzip2 + mcf", gain: 1.3623 },
  { label: "astar + lbm", gain: 1.3950 },
  { label: "bzip2 + lbm", gain: 0.5790 },
  { label: "mcf + lbm", gain: 1.1559 }
];

const homoPairs = [
  { label: "astar + astar", gain: 4.2142 },
  { label: "mcf + mcf", gain: 1.7809 },
  { label: "lbm + lbm", gain: 0.1705 },
  { label: "bzip2 + bzip2", gain: 0.0515 }
];

const phase2 = [
  { metric: "combined_score", phase1: -6.2665, phase2: -3.0105, delta: 3.2559 },
  { metric: "Mean gain", phase1: -0.2240, phase2: -0.0289, delta: 0.1951 },
  { metric: "Worst-case gain", phase1: -1.0157, phase2: -0.5410, delta: 0.4747 }
];

window.posterData = { singleCore, heteroPairs, homoPairs, phase2 };
