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
<svg viewBox="0 0 80 80" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
  <defs>
    <radialGradient id="wispBody" cx="42%" cy="36%" r="70%">
      <stop offset="0%" stop-color="#fff3d6"/>
      <stop offset="55%" stop-color="#ffb347"/>
      <stop offset="100%" stop-color="#ff7a00"/>
    </radialGradient>
  </defs>

  <g class="wisp__spark"><circle cx="18" cy="40" r="2.6" fill="#ffd28a"/></g>
  <g class="wisp__spark"><circle cx="12" cy="50" r="2" fill="#ffb347"/></g>
  <g class="wisp__spark"><circle cx="22" cy="58" r="1.6" fill="#ffd28a"/></g>
  <g class="wisp__spark"><circle cx="10" cy="32" r="1.8" fill="#ffa040"/></g>

  <g class="wisp__body">
    <ellipse class="wisp__glow" cx="40" cy="40" rx="32" ry="29" fill="#ff7a00" opacity="0.4"/>

    <g class="wisp__wing-l">
      <ellipse cx="20" cy="26" rx="13" ry="9" fill="#ffd9a8" stroke="#c96a00" stroke-width="2.5"
               transform="rotate(26 20 26)"/>
      <ellipse cx="16" cy="22" rx="5" ry="3.5" fill="#fff3d6" opacity="0.6" transform="rotate(26 16 22)"/>
    </g>
    <g class="wisp__wing-r">
      <ellipse cx="60" cy="26" rx="13" ry="9" fill="#ffd9a8" stroke="#c96a00" stroke-width="2.5"
               transform="rotate(-26 60 26)"/>
      <ellipse cx="64" cy="22" rx="5" ry="3.5" fill="#fff3d6" opacity="0.6" transform="rotate(-26 64 22)"/>
    </g>

    <circle cx="40" cy="42" r="20" fill="url(#wispBody)" stroke="#c96a00" stroke-width="2.8"/>

    <g class="wisp__eyes">
      <ellipse cx="33" cy="40" rx="4.4" ry="5.6" fill="#3d2300"/>
      <ellipse cx="47" cy="40" rx="4.4" ry="5.6" fill="#3d2300"/>
      <circle cx="34.5" cy="38" r="1.8" fill="#fff"/>
      <circle cx="48.5" cy="38" r="1.8" fill="#fff"/>
    </g>

    <ellipse class="wisp__eyebrow-l" cx="33" cy="31.5" rx="5" ry="2" fill="#c96a00" transform="rotate(-8 33 31.5)"/>
    <ellipse class="wisp__eyebrow-r" cx="47" cy="31.5" rx="5" ry="2" fill="#c96a00" transform="rotate(8 47 31.5)"/>

    <path d="M35 49 Q40 54 45 49" stroke="#3d2300" stroke-width="2.6" fill="none"
          stroke-linecap="round"/>

    <ellipse cx="29" cy="48" rx="3" ry="1.8" fill="#e8680a" opacity="0.55"/>
    <ellipse cx="51" cy="48" rx="3" ry="1.8" fill="#e8680a" opacity="0.55"/>
  </g>

  <g class="wisp__tail">
    <path d="M40 60 Q48 70 38 78 Q52 80 58 68 Q54 82 44 88" fill="none" stroke="#c96a00"
          stroke-width="9" stroke-linecap="round"/>
    <path d="M40 60 Q48 70 38 78 Q52 80 58 68 Q54 82 44 88" fill="none" stroke="#ff8f2a"
          stroke-width="5.5" stroke-linecap="round"/>
    <path d="M40 60 Q48 70 38 78 Q52 80 58 68 Q54 82 44 88" fill="none" stroke="#ffd28a"
          stroke-width="2.2" stroke-linecap="round"/>
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