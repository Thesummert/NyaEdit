# MathJax bundle

`tex-svg.js` is generated from `build.js` using MathJax 3.2.2 (Apache-2.0),
mhchemparser 4.2.1 (Apache-2.0), and esbuild 0.25.12. The corresponding
licenses are in this directory. To rebuild, install those versions with npm,
then run the esbuild command in `build.js`. The bundle embeds the MathJax TeX
input packages and SVG font data; it has no runtime network dependency.
