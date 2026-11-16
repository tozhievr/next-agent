#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(binding=0) uniform Matrices{mat4 model;mat4 view;mat4 projection;};
layout(location=0) out vec3 outNormal;
void main(){gl_Position=projection*view*model*vec4(position,1.0);outNormal=normalize(transpose(inverse(mat3(model)))*normal);}
