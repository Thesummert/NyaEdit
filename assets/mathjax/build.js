// Build with mathjax-full 3.2.2 and esbuild 0.25.12:
// esbuild build.js --bundle --platform=browser --target=es2015 --minify --outfile=tex-svg.js
const {mathjax} = require('mathjax-full/js/mathjax.js');
const {TeX} = require('mathjax-full/js/input/tex.js');
const {SVG} = require('mathjax-full/js/output/svg.js');
const {liteAdaptor} = require('mathjax-full/js/adaptors/liteAdaptor.js');
const {RegisterHTMLHandler} = require('mathjax-full/js/handlers/html.js');
const {AllPackages} = require('mathjax-full/js/input/tex/AllPackages.js');
const adaptor = liteAdaptor();
RegisterHTMLHandler(adaptor);
let doc;
globalThis.nyaReset = function() {
  const tex = new TeX({packages: AllPackages.filter(p => p !== 'noerrors' && p !== 'noundefined'), maxMacros: 1000});
  doc = mathjax.document('', {InputJax: tex, OutputJax: new SVG({fontCache: 'local'})});
};
globalThis.nyaRender = function(input, display) {
  const node = doc.convert(input, {display, em: 16, ex: 8, containerWidth: 1280});
  return adaptor.outerHTML(adaptor.firstChild(node));
};
