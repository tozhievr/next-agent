#version 450
layout(location=0) out vec4 outColor;
const float PI=3.141592653589793;
vec3 evaluateSpecular(vec3 N,vec3 V,vec3 L,vec3 F0,float roughness){N=normalize(N);V=normalize(V);L=normalize(L);float nv=max(dot(N,V),0.0),nl=max(dot(N,L),0.0);if(nv<=0.0||nl<=0.0||length(V+L)<0.00001)return vec3(0);vec3 H=normalize(V+L);float nh=max(dot(N,H),0.0),vh=max(dot(V,H),0.0);float a=pow(clamp(roughness,0.045,1.0),2.0),a2=a*a;float d=nh*nh*(a2-1.0)+1.0;float D=a2/(PI*d*d);float gv=2.0*nv/(nv+sqrt(a2+(1.0-a2)*nv*nv));float gl=2.0*nl/(nl+sqrt(a2+(1.0-a2)*nl*nl));vec3 F=F0+(vec3(1)-F0)*pow(1.0-vh,5.0);return D*gv*gl*F/max(4.0*nv*nl,0.00001);}
void main(){outColor=vec4(evaluateSpecular(vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),vec3(0.04),0.5),1);}
