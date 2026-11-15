import type {Result} from './interfaces/api';
export function mapResult<T,E,U>(r:Result<T,E>,f:(v:T)=>U):Result<U,E>{return r.ok?{ok:true,value:f(r.value)}:r;}
export function flatMapResult<T,E,U>(r:Result<T,E>,f:(v:T)=>Result<U,E>):Result<U,E>{return r.ok?f(r.value):r;}
