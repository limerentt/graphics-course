#version 430

// layout(local_size_x = 8, local_size_y = 8) in;

// layout(binding = 0, rgba8) uniform image2D resultImage;

layout(binding = 0) uniform sampler2D iChannel0;
layout(binding = 1) uniform sampler2D iChannel1;

layout(push_constant) uniform params_t
{
  vec2 iResolution;
  float iTime;
} consts;

layout(location = 0) out vec4 out_fragColor;

// From Inigo Quilez: https://iquilezles.org/articles/distfunctions/
float sdCappedCylinder( vec3 p, float h, float r )
{
  vec2 d = abs(vec2(length(p.xz),p.y)) - vec2(r,h);
  return min(max(d.x,d.y),0.0) + length(max(d,0.0));
}

// From Inigo Quilez: https://iquilezles.org/articles/distfunctions/
float sdPyramid( vec3 p, float h )
{
  float m2 = h*h + 0.25;
    
  p.xz = abs(p.xz);
  p.xz = (p.z>p.x) ? p.zx : p.xz;
  p.xz -= 0.5;

  vec3 q = vec3( p.z, h*p.y - 0.5*p.x, h*p.x + 0.5*p.y);
   
  float s = max(-q.x,0.0);
  float t = clamp( (q.y-0.5*p.z)/(m2+0.25), 0.0, 1.0 );
    
  float a = m2*(q.x+s)*(q.x+s) + q.y*q.y;
  float b = m2*(q.x+0.5*t)*(q.x+0.5*t) + (q.y-m2*t)*(q.y-m2*t);
    
  float d2 = min(q.y,-q.x*m2-q.y*0.5) > 0.0 ? 0.0 : min(a,b);
    
  return sqrt( (d2+q.z*q.z)/m2 ) * sign(max(q.z,-p.y));
}

// From Inigo Quilez: https://iquilezles.org/articles/distfunctions/
float sdVesicaSegment( in vec3 p, in vec3 a, in vec3 b, in float w )
{
    vec3  c = (a+b)*0.5;
    float l = length(b-a);
    vec3  v = (b-a)/l;
    float y = dot(p-c,v);
    vec2  q = vec2(length(p-c-y*v),abs(y));
    
    float r = 0.5*l;
    float d = 0.5*(r*r-w*w)/w;
    vec3  h = (r*q.x<d*(q.y-r)) ? vec3(0.0,r,0.0) : vec3(-d,0.0,d+w);
 
    return length(q-h.xy) - h.z;
}

float justSDF(in vec3 point) {
    // sorry for copypasta
    const int n = 5;
    float SDFs[n];
    SDFs[0] = sdPyramid(point - vec3(0, -0.5, 2), 1.0) - 0.05;
    
    SDFs[1] = sdCappedCylinder(point - vec3(0, 0.75, 2), 0.2, 0.1) - 0.01;
    
    SDFs[2] = sdCappedCylinder(point - vec3(0, 0.55, 2), 0.01, 0.2) - 0.01;
    
    vec3 eye = vec3(0, -0.15, 1.5);
    SDFs[3] = sdVesicaSegment(point, eye + vec3(-0.2, 0, 0), eye + vec3(+0.2, 0, 0), 0.12) - 0.01;
    
    SDFs[4] = sdVesicaSegment(point, eye + vec3(0, -0.08, -0.14), eye + vec3(0, +0.1, -0.14), 0.025) - 0.01;
    
    float sdf = SDFs[0];
    for (int i = 0; i < n; i++) {
        if (sdf > SDFs[i]) {
            sdf = SDFs[i];
        }
    }
    
    return sdf;
}

vec3 normal(in vec3 p) {
    float h = 0.01;
    float dfdx = (justSDF(p + vec3(h, 0.0, 0.0)) - justSDF(p - vec3(h, 0.0, 0.0))) / (2.0 * h);
    float dfdy = (justSDF(p + vec3(0.0, h, 0.0)) - justSDF(p - vec3(0.0, h, 0.0))) / (2.0 * h);
    float dfdz = (justSDF(p + vec3(0.0, 0.0, h)) - justSDF(p - vec3(0.0, 0.0, h))) / (2.0 * h);
    
    return normalize(vec3(dfdx, dfdy, dfdz));
}

void calcSDF(in vec3 point, out float sdf, out vec4 col) {
    const int n = 5;
    float SDFs[n];
    vec4 colors[n];
    SDFs[0] = sdPyramid(point - vec3(0, -0.5, 2), 1.0) - 0.05;
    vec3 nrm = abs(normal(point));
    colors[0] = texture(iChannel1, (point.xy + vec2(1)) / 2.0) * nrm.z +
    texture(iChannel1, (point.xz + vec2(1)) / 2.0) * nrm.y / 3.0 +
    texture(iChannel1, (point.yz + vec2(1)) / 2.0) * nrm.x / 3.0;
    
    SDFs[1] = sdCappedCylinder(point - vec3(0, 0.75, 2), 0.2, 0.1) - 0.01;
    colors[1] = vec4(0.05, 0.05, 0.05, 1);
    
    SDFs[2] = sdCappedCylinder(point - vec3(0, 0.55, 2), 0.01, 0.2) - 0.01;
    colors[2] = vec4(0.05, 0.05, 0.05, 1);
    
    vec3 eye = vec3(0, -0.15, 1.5);
    SDFs[3] = sdVesicaSegment(point, eye + vec3(-0.2, 0, 0), eye + vec3(+0.2, 0, 0), 0.12) - 0.01;
    colors[3] = vec4(0.8, 0.8, 0.8, 1);
    
    SDFs[4] = sdVesicaSegment(point, eye + vec3(0, -0.08, -0.14), eye + vec3(0, +0.1, -0.14), 0.025) - 0.01;
    colors[4] = vec4(0, 0, 0.01, 1);
    
    sdf = SDFs[0];
    col = colors[0];
    for (int i = 0; i < n; i++) {
        if (sdf > SDFs[i]) {
            sdf = SDFs[i];
            col = colors[i];
        }
    }
}

bool castRay(in vec2 uv, inout vec4 surfCol, inout vec3 point) {
    vec3 ray = normalize(vec3(uv, 1));
    point = vec3(0);
    
    while(length(point) < 1000.0) {
        float sdf;
        calcSDF(point, sdf, surfCol);
        
        if (sdf < 0.001) {
            return true;
        }
        
        point += ray * sdf;
    }
    
    surfCol = vec4(0.1, (-uv.y + 0.7) / 1.7, (-uv.y + 0.7) / 1.7, 1.0);
    return false;
}

void main()
{
    vec2 uv = (gl_FragCoord.xy - consts.iResolution / 2.0) / min(consts.iResolution.x, consts.iResolution.y);
    uv.y = -uv.y;

    vec4 surfCol;
    vec3 impactPoint;
    vec4 ambient, diffuse, specular;
    if (castRay(uv, surfCol, impactPoint)) {
        ambient = vec4(0.6, 0.6, 0.6, 1.0) * surfCol / 2.0;
        
        vec3 nrm = normal(impactPoint);
        float intensity = max(dot(nrm, vec3(-1, 1, -1)), 0.0) / 2.0;
        diffuse = intensity * surfCol;
        
        vec3 reflected = reflect(vec3(-1, -1, sin(consts.iTime) / 2.0), nrm);
        specular = vec4(pow(max(dot(vec3(uv, 1), -reflected), 0.0), 10.0)) / 17.0;
    } else {
        ambient = texture(iChannel0, uv);
        diffuse = vec4(0);
        specular = vec4(0);
    }
    out_fragColor = ambient + diffuse + specular;
}
