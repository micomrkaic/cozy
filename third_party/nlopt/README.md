# third_party/nlopt — NLopt for the browser build

A vendored subset of [NLopt 2.11.0](https://github.com/stevengj/nlopt)
(Steven G. Johnson and contributors; see COPYING / COPYRIGHT — LGPL core
with MIT-licensed algorithm files): the C algorithms and the C API,
compiled by emcc into the Cozy wasm bundle so `nlmin` and the optim
package's SLSQP dispatch work in the browser exactly as they do natively.
The C++ algorithms (StoGO, AGS) are omitted; `nlopt_config.h` is written
by hand for wasm32. Native builds never use this directory — they link
the system libnlopt (`make OPTIM=nlopt`). Vendored at upstream commit 673d2504f819. Refresh by re-copying
`src/algs/<c dirs>`, `src/api`, `src/util` from upstream.
