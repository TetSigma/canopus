/**
 * imu_dashboard.c — 3D IMU visualizer with axis calibration UI
 */

#include "imu_dashboard.h"
#include "imu_service.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "imu_dash";
static httpd_handle_t s_server = NULL;

static const char INDEX_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=utf-8>"
    "<title>IMU 3D</title>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<style>"
    "*{margin:0;padding:0;box-sizing:border-box}"
    "body{background:#0a0a0a;color:#ccc;font-family:system-ui,sans-serif}"
    "#c{display:block;width:100vw;height:100vh;position:fixed;top:0;left:0}"
    "#panel{position:fixed;top:0;right:0;width:220px;background:rgba(0,0,0,.85);"
    " padding:12px;font-size:11px;z-index:10;border-left:1px solid #222}"
    "#panel h3{font-size:11px;color:#666;letter-spacing:1px;margin-bottom:8px}"
    ".row{display:flex;align-items:center;gap:6px;margin-bottom:6px}"
    ".row label{width:24px;color:#888}"
    "select{background:#1a1a1a;color:#ccc;border:1px solid #333;padding:2px 4px;"
    " font-size:11px;border-radius:3px;flex:1}"
    ".val{width:52px;text-align:right;color:#aaa;font-variant-numeric:tabular-nums}"
    "#dot{width:7px;height:7px;border-radius:50%;background:#333;display:inline-block;"
    " margin-right:6px;transition:background .3s}"
    "#dot.live{background:#22cc44}"
    "hr{border:none;border-top:1px solid #222;margin:8px 0}"
    ".tip{color:#555;font-size:10px;line-height:1.5}"
    "</style></head><body>"
    "<canvas id=c></canvas>"
    "<div id=panel>"
    " <span id=dot></span><span id=fps style='color:#555'>-- fps</span>"
    " <hr>"
    " <h3>AXIS MAPPING</h3>"
    " <div class=row><label>X→</label>"
    "  <select id=mx><option>ax</option><option>ay</option><option>az</option>"
    "   <option>-ax</option><option>-ay</option><option>-az</option></select></div>"
    " <div class=row><label>Y→</label>"
    "  <select id=my><option>ay</option><option>ax</option><option>az</option>"
    "   <option>-ay</option><option>-ax</option><option>-az</option></select></div>"
    " <div class=row><label>Z→</label>"
    "  <select id=mz><option>az</option><option>ax</option><option>ay</option>"
    "   <option>-az</option><option>-ax</option><option>-ay</option></select></div>"
    " <hr>"
    " <h3>RAW VALUES</h3>"
    " <div class=row><label>Ax</label><span id=ax class=val>0.000</span><span style='color:#555'> g</span></div>"
    " <div class=row><label>Ay</label><span id=ay class=val>0.000</span><span style='color:#555'> g</span></div>"
    " <div class=row><label>Az</label><span id=az class=val>0.000</span><span style='color:#555'> g</span></div>"
    " <div class=row><label>Gx</label><span id=gx class=val>0.0</span><span style='color:#555'> °/s</span></div>"
    " <div class=row><label>Gy</label><span id=gy class=val>0.0</span><span style='color:#555'> °/s</span></div>"
    " <div class=row><label>Gz</label><span id=gz class=val>0.0</span><span style='color:#555'> °/s</span></div>"
    " <hr>"
    " <div class=tip>"
    "  Hold watch FLAT face-up.<br>"
    "  Az should read ~+1.0g.<br>"
    "  Tilt right: Ax should go +.<br>"
    "  Tilt forward: Ay should go +.<br>"
    "  Adjust mapping until box matches."
    " </div>"
    "</div>"

    "<script src=https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js></script>"
    "<script>"
    "const renderer=new THREE.WebGLRenderer({canvas:document.getElementById('c'),antialias:true});"
    "renderer.setPixelRatio(Math.min(devicePixelRatio,2));"
    "renderer.setSize(innerWidth,innerHeight);"
    "const scene=new THREE.Scene();"
    "scene.background=new THREE.Color(0x0a0a0a);"
    "const camera=new THREE.PerspectiveCamera(45,innerWidth/innerHeight,.1,100);"
    "camera.position.set(0,1.5,4.5);camera.lookAt(0,0,0);"
    "scene.add(new THREE.AmbientLight(0xffffff,.4));"
    "const sun=new THREE.DirectionalLight(0xffffff,.9);sun.position.set(3,5,4);scene.add(sun);"
    "scene.add(new THREE.GridHelper(10,20,0x1f1f1f,0x1a1a1a));"
    "scene.children[scene.children.length-1].position.y=-2;"

    /* Watch model */
    "const mat=new THREE.MeshStandardMaterial({color:0x1a1a1a,roughness:.4,metalness:.7});"
    "const scrMat=new THREE.MeshStandardMaterial({color:0x0a2040,roughness:.1,emissive:0x051020,emissiveIntensity:1});"
    "const bandMat=new THREE.MeshStandardMaterial({color:0x0d0d0d,roughness:.9,metalness:.05});"
    "const watch=new THREE.Group();"
    "[[new THREE.BoxGeometry(1.4,1.6,.35),mat,0,0,0],"
    " [new THREE.BoxGeometry(1.1,1.25,.02),scrMat,0,0,.18],"
    " [new THREE.BoxGeometry(.9,.7,.28),bandMat,0,1.12,0],"
    " [new THREE.BoxGeometry(.9,.7,.28),bandMat,0,-1.12,0]"
    "].forEach(([g,m,x,y,z])=>{"
    " const mesh=new THREE.Mesh(g,m);mesh.position.set(x,y,z);watch.add(mesh);});"
    "const crown=new THREE.Mesh(new THREE.CylinderGeometry(.065,.065,.28,16),"
    " new THREE.MeshStandardMaterial({color:0x3a3a3a,roughness:.3,metalness:.8}));"
    "crown.rotation.z=Math.PI/2;crown.position.set(.78,.15,0);watch.add(crown);"
    "scene.add(watch);"

    /* Axis arrows for reference */
    "const arrowGroup=new THREE.Group();"
    "[[1,0,0,0xff4444,'X'],[0,1,0,0x44ff44,'Y'],[0,0,1,0x4488ff,'Z']].forEach(([x,y,z,c,n])=>{"
    " const arr=new THREE.ArrowHelper(new THREE.Vector3(x,y,z),new THREE.Vector3(0,0,0),1.2,c,.2,.1);"
    " arrowGroup.add(arr);});"
    "arrowGroup.position.set(-3,-.5,0);scene.add(arrowGroup);"

    /* Complementary filter */
    "let qw=1,qx=0,qy=0,qz=0;"
    "let lastT=performance.now(),frames=0,fpsT=performance.now();"

    "function norm(){const n=Math.sqrt(qw*qw+qx*qx+qy*qy+qz*qz);qw/=n;qx/=n;qy/=n;qz/=n;}"

    "function intGyro(gx,gy,gz,dt){"
    " const D=Math.PI/180;"
    " const hx=gx*D*dt/2,hy=gy*D*dt/2,hz=gz*D*dt/2;"
    " const nw=qw-hx*qx-hy*qy-hz*qz;"
    " const nx=qx+hx*qw+hy*qz-hz*qy;"
    " const ny=qy-hx*qz+hy*qw+hz*qx;"
    " const nz=qz+hx*qy-hy*qx+hz*qw;"
    " qw=nw;qx=nx;qy=ny;qz=nz;norm();}"

    "function correctAccel(ax,ay,az){"
    " const n=Math.sqrt(ax*ax+ay*ay+az*az);"
    " if(n<.5||n>1.8)return;"
    " const A=.015;" /* alpha */
    " qw=qw*(1-A)+1*A;qx=qx*(1-A)+(ax/n)*A;"
    " qy=qy*(1-A)+(ay/n)*A;qz=qz*(1-A)+(az/n)*A;norm();}"

    /* Axis mapping from dropdowns */
    "function getMapped(d){"
    " const src={ax:d.ax,ay:d.ay,az:d.az,'-ax':-d.ax,'-ay':-d.ay,'-az':-d.az};"
    " return{"
    "  x:src[document.getElementById('mx').value],"
    "  y:src[document.getElementById('my').value],"
    "  z:src[document.getElementById('mz').value]};"
    "}"
    "function getGyroMapped(d){"
    " const src={ax:d.gx,ay:d.gy,az:d.gz,'-ax':-d.gx,'-ay':-d.gy,'-az':-d.gz};"
    " return{"
    "  x:src[document.getElementById('mx').value.replace('a','g').replace('-g','-g')]||d.gx,"
    "  y:d.gy,z:d.gz};" /* simple: gyro follows same swap as accel */
    "}"

    /* Poll */
    "let fd={ax:0,ay:0,az:1,gx:0,gy:0,gz:0};"
    "async function poll(){"
    " try{"
    "  const r=await fetch('/data');if(!r.ok)throw 0;"
    "  fd=await r.json();"
    "  document.getElementById('dot').className='live';"
    "  document.getElementById('ax').textContent=fd.ax.toFixed(3);"
    "  document.getElementById('ay').textContent=fd.ay.toFixed(3);"
    "  document.getElementById('az').textContent=fd.az.toFixed(3);"
    "  document.getElementById('gx').textContent=fd.gx.toFixed(1);"
    "  document.getElementById('gy').textContent=fd.gy.toFixed(1);"
    "  document.getElementById('gz').textContent=fd.gz.toFixed(1);"
    " }catch(e){document.getElementById('dot').className='';}"
    " setTimeout(poll,50);}"
    "poll();"

    /* Animate */
    "function animate(){"
    " requestAnimationFrame(animate);"
    " const now=performance.now(),dt=(now-lastT)/1e3;lastT=now;"
    " const m=getMapped(fd);"
    " intGyro(fd.gx,fd.gy,fd.gz,dt);"
    " correctAccel(m.x,m.y,m.z);"
    " watch.quaternion.set(qx,qy,qz,qw);"
    " frames++;if(now-fpsT>1000){"
    "  document.getElementById('fps').textContent=frames+' fps';"
    "  frames=0;fpsT=now;}"
    " renderer.render(scene,camera);}"
    "animate();"

    "window.addEventListener('resize',()=>{"
    " camera.aspect=innerWidth/innerHeight;"
    " camera.updateProjectionMatrix();"
    " renderer.setSize(innerWidth,innerHeight);});"
    "</script></body></html>";

static esp_err_t data_handler(httpd_req_t *req)
{
    imu_data_t d = imu_get_data();
    char buf[200];
    int len = snprintf(buf, sizeof(buf),
                       "{\"ax\":%.4f,\"ay\":%.4f,\"az\":%.4f,"
                       "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,\"steps\":%lu}",
                       d.accel.x, d.accel.y, d.accel.z,
                       d.gyro.x, d.gyro.y, d.gyro.z,
                       (unsigned long)d.steps);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
    return ESP_OK;
}

esp_err_t imu_dashboard_start(void)
{
    if (s_server)
        return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_open_sockets = 4;
    if (httpd_start(&s_server, &cfg) != ESP_OK)
        return ESP_FAIL;
    httpd_uri_t ui = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    httpd_uri_t data = {.uri = "/data", .method = HTTP_GET, .handler = data_handler};
    httpd_register_uri_handler(s_server, &ui);
    httpd_register_uri_handler(s_server, &data);
    esp_netif_ip_info_t ip;
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (n && esp_netif_get_ip_info(n, &ip) == ESP_OK)
        ESP_LOGI(TAG, "open http://" IPSTR, IP2STR(&ip.ip));
    return ESP_OK;
}

esp_err_t imu_dashboard_stop(void)
{
    if (!s_server)
        return ESP_OK;
    httpd_stop(s_server);
    s_server = NULL;
    return ESP_OK;
}