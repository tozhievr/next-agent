const forbidden=new Set(['__proto__','constructor','prototype']);
const plain=(x:unknown):x is Record<string,unknown>=>x!==null&&typeof x==='object'&&(Object.getPrototypeOf(x)===Object.prototype||Object.getPrototypeOf(x)===null);
function copy(x:unknown):unknown {if(Array.isArray(x))return x.map(copy);if(plain(x)){const r:Record<string,unknown>={};for(const k of Object.keys(x))if(!forbidden.has(k))r[k]=copy(x[k]);return r;}return x;}
export function deepMerge<T extends Record<string,unknown>>(base:T,patch:Record<string,unknown>):T {const r=copy(base) as Record<string,unknown>;for(const k of Object.keys(patch)){if(forbidden.has(k))continue;const v=patch[k];r[k]=plain(v)&&plain(r[k])?deepMerge(r[k] as Record<string,unknown>,v):copy(v);}return r as T;}
