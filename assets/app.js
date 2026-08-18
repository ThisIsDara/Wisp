/* ---------- reveal on scroll ---------- */

const io = new IntersectionObserver(
  (es) => es.forEach((e) => e.isIntersecting && e.target.classList.add("in")),
  { threshold: 0.12 }
);

document.querySelectorAll(".reveal").forEach((el) => io.observe(el));

/* ---------- wisp easter egg ----------
   A cartoon wisp occasionally drifts across the page: glowing orb body,
   flapping wings, flickering flame tail, blinking eyes, sparkle trail.
   Random direction, height, and timing on every flight. */

const WISP_SVG = `
<svg viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
  <defs>
    <radialGradient id="wispBody" cx="42%" cy="38%" r="70%">
      <stop offset="0%" stop-color="#ffd9a8"/>
      <stop offset="60%" stop-color="#ff8f2a"/>
      <stop offset="100%" stop-color="#f26600"/>
    </radialGradient>
  </defs>

  <g class="wisp__orbit wisp__orbit--1"><circle cx="46" cy="18" r="2.6" fill="#ffd28a"/></g>
  <g class="wisp__orbit wisp__orbit--2"><circle cx="18" cy="46" r="2" fill="#ffb347"/></g>
  <g class="wisp__orbit wisp__orbit--3"><circle cx="50" cy="46" r="1.6" fill="#ffd28a"/></g>

  <g class="wisp__body">
    <ellipse class="wisp__glow" cx="32" cy="32" rx="22" ry="22" fill="#ff7a00" opacity="0.35"/>
    <circle cx="32" cy="32" r="15" fill="url(#wispBody)"/>
  </g>
</svg>`;

(function () {
  if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) return;

  const wisp = document.createElement("div");
  wisp.className = "wisp";
  wisp.innerHTML = WISP_SVG;
  document.body.appendChild(wisp);

  const MIN_TOP = 12, MAX_TOP = 72;   // % of viewport height
  const MIN_DUR = 5, MAX_DUR = 9;     // seconds per flight
  const FIRST_DELAY = 3000;           // first flight shortly after load
  const MIN_GAP = 15000, MAX_GAP = 40000; // gap between flights

  function fly() {
    if (document.hidden) return;
    const left = Math.random() < 0.5;
    wisp.classList.remove("wisp--l", "wisp--r");
    void wisp.offsetWidth; // restart animations
    wisp.classList.add(left ? "wisp--l" : "wisp--r");
    wisp.style.top = (MIN_TOP + Math.random() * (MAX_TOP - MIN_TOP)) + "%";
    wisp.style.animationDuration = (MIN_DUR + Math.random() * (MAX_DUR - MIN_DUR)) + "s";
  }

  function schedule(first) {
    const gap = first ? FIRST_DELAY : MIN_GAP + Math.random() * (MAX_GAP - MIN_GAP);
    setTimeout(() => { fly(); schedule(false); }, gap);
  }

  schedule(true);
})();