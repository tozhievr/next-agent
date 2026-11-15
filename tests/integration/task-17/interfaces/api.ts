export type Cart=Readonly<Record<string,number>>;
export type CartEvent={type:'add',sku:string,quantity:number}|{type:'remove',sku:string}|{type:'setQuantity',sku:string,quantity:number};
