#version 450
layout(location=0) in vec4 vColor;
layout(location=0) out vec4 outColor;
void main(){float luminance=dot(vColor.rgb,vec3(0.2126,0.7152,0.0722));outColor=vec4(vec3(luminance),vColor.a);}
