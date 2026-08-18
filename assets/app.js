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
    <radialGradient id="wispBody" cx="42%" cy="38%" r="65%">
      <stop offset="0%" stop-color="#ffe9c4"/>
      <stop offset="55%" stop-color="#ffb347"/>
      <stop offset="100%" stop-color="#ff7a00"/>
    </radialGradient>
  </defs>

  <g class="wisp__spark"><circle cx="16" cy="34" r="2" fill="#ffd28a"/></g>
  <g class="wisp__spark"><circle cx="11" cy="42" r="1.5" fill="#ffb347"/></g>
  <g class="wisp__spark"><circle cx="18" cy="48" r="1.2" fill="#ffd28a"/></g>
  <g class="wisp__spark"><circle cx="9" cy="28" r="1.3" fill="#ffa040"/></g>

  <g class="wisp__body">
    <ellipse class="wisp__glow" cx="32" cy="34" rx="24" ry="22" fill="#ff7a00" opacity="0.35"/>

    <g class="wisp__wing-l">
      <ellipse cx="16" cy="24" rx="10" ry="7" fill="#ffc98a" opacity="0.85" transform="rotate(24 16 24)"/>
    </g>
    <g class="wisp__wing-r">
      <ellipse cx="48" cy="24" rx="10" ry="7" fill="#ffc98a" opacity="0.85" transform="rotate(-24 48 24)"/>
    </g>

    <circle cx="32" cy="34" r="16" fill="url(#wispBody)"/>

    <g class="wisp__eyes">
      <circle cx="27" cy="32" r="2.1" fill="#3d2300"/>
      <circle cx="37" cy="32" r="2.1" fill="#3d2300"/>
      <circle cx="27.7" cy="31.2" r="0.7" fill="#fff"/>
      <circle cx="37.7" cy="31.2" r="0.7" fill="#fff"/>
    </g>

    <path class="wisp__mouth" d="M28.5 38.5 Q32 41 35.5 38.5" stroke="#3d2300" stroke-width="1.6"
          fill="none" stroke-linecap="round"/>

    <ellipse cx="24.5" cy="37" rx="2" ry="1.1" fill="#e8680a" opacity="0.45"/>
    <ellipse cx="39.5" cy="37" rx="2" ry="1.1" fill="#e8680a" opacity="0.45"/>
  </g>

  <g class="wisp__tail">
    <path d="M32 48 Q36 56 30 62 Q40 60 44 50" fill="none" stroke="#ff8f2a" stroke-width="5"
          stroke-linecap="round"/>
    <path d="M32 48 Q36 56 30 62 Q40 60 44 50" fill="none" stroke="#ffd28a" stroke-width="2.2"
          stroke-linecap="round"/>
  </g>
</svg>`;

(function () {
  if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) return;

  const wisp = document.createElement("div");
  wisp.className = "wisp";
  wisp.innerHTML = WISP_SVG;
  document.body.appendChild(wisp);

  const MIN_TOP = 12, MAX_TOP = 72;   // % of viewport height
  const MIN_DUR = 11, MAX_DUR = 20;   // seconds per flight
  const FIRST_DELAY = 6000;           // first flight shortly after load
  const MIN_GAP = 35000, MAX_GAP = 90000; // gap between flights

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