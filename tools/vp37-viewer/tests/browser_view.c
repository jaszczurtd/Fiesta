/* Generate regression probes in a real browser; assertions are owned by C.
 * Synthetic protocol fixtures contain no firmware or hardware test data. */
#include "browser_view.h"
#include "hal/network/hal_tcp.h"
#include "hal/system/hal_system.h"
#include <stdio.h>
#include <string.h>

static bool get(uint16_t port, const char *path, char *out, size_t capacity,
                hal_status_t (*poll_server)(void *), void *server) {
  hal_tcp_socket_t socket = NULL;
  hal_net_endpoint_t address = {0};
  address.family = HAL_NET_AF_INET;
  address.addr_len = HAL_NET_IPV4_ADDR_LEN;
  address.addr[0] = 127;
  address.addr[3] = 1;
  address.port = port;
  char request[256];
  const int length = snprintf(
      request, sizeof(request),
      "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", path);
  size_t used = 0;
  bool ok = false;
  do {
    if (length <= 0 || (size_t)length >= sizeof(request) ||
        hal_tcp_socket_open_ex(&socket) != HAL_OK ||
        hal_tcp_socket_connect_ex(socket, &address, 1000) != HAL_OK ||
        hal_tcp_socket_send(socket, request, (size_t)length) != length)
      break;
    for (unsigned i = 0; i < 2000 && used < capacity - 1; ++i) {
      if (poll_server(server) != HAL_OK)
        break;
      size_t count = 0;
      const hal_status_t status = hal_tcp_socket_recv_ex(
          socket, out + used, capacity - 1 - used, 0, &count);
      used += count;
      if (status == HAL_OK && !count) {
        ok = true;
        break;
      }
      if (status != HAL_OK && status != HAL_EAGAIN && status != HAL_ETIMEOUT)
        break;
      hal_delay_ms(1);
    }
  } while (false);
  hal_tcp_socket_close(socket);
  out[used] = '\0';
  return ok && strncmp(out, "HTTP/1.1 200 OK", 15) == 0;
}
static bool include_file(FILE *out, const char *directory, const char *name) {
  char path[4096], bytes[4096];
  const int n = snprintf(path, sizeof(path), "%s/%s", directory, name);
  if (n < 0 || (size_t)n >= sizeof(path))
    return false;
  FILE *input = NULL;
#ifdef _WIN32
  if (fopen_s(&input, path, "rb") != 0)
    return false;
#else
  input = fopen(path, "rb");
#endif
  if (!input)
    return false;
  size_t count;
  while ((count = fread(bytes, 1, sizeof(bytes), input)) != 0)
    if (fwrite(bytes, 1, count, out) != count) {
      fclose(input);
      return false;
    }
  const bool ok = !ferror(input);
  fclose(input);
  return ok;
}
static void check(FILE *out, const char *expression, const char *label) {
  fprintf(out, "check((%s), '%s');\n", expression, label);
}

bool vp37v_browser_fixture(uint16_t port, hal_status_t (*poll_server)(void *),
                           void *server, const char *web, const char *messages,
                           const char *output) {
  static char response[262144];
  const char *pages[] = {"/", "/live"};
  for (size_t i = 0; i < COUNTOF(pages); ++i)
    if (!get(port, pages[i], response, sizeof(response), poll_server, server) ||
        !strstr(response, "VP37.startLive()") ||
        !strstr(response, "/live-assets/live.js"))
      return false;
  if (!get(port, "/api/fields", response, sizeof(response), poll_server,
           server))
    return false;
  const char *body = strstr(response, "\r\n\r\n");
  if (!body || !strstr(body, "\"key\":\"scanus\""))
    return false;
  FILE *out = NULL;
#ifdef _WIN32
  if (fopen_s(&out, output, "wb") != 0)
    return false;
#else
  out = fopen(output, "wb");
#endif
  if (!out)
    return false;
  fputs("<!doctype html><meta charset=\"utf-8\"><style>", out);
  bool ok = include_file(out, web, "viewer.css");
  fputs("</style><main id=\"app\"></main><pre "
        "id=\"result\">PENDING</pre><script>",
        out);
  ok = include_file(out, web, "i18n.js") && ok;
  fputs("</script><script>", out);
  ok = include_file(out, web, "viewer.js") && ok;
  fputs("</script><script>", out);
  ok = include_file(out, web, "live.js") && ok;
  fprintf(out, "</script><script>const catalog=%s;const fixture=%s", body + 4,
          messages);
  fputs(
      ";let checks=0;function check(ok,label){if(!ok)throw "
      "Error(label);checks++;}\n"
      "(async()=>{try {"
      "const query=new URLSearchParams(location.search);"
      "if(query.has('lang-step')){"
      "const "
      "step=Number(query.get('lang-step'));checks=Number(query.get('checks'));"
      "const "
      "header=document.createElement('header');header.className='top';document."
      "body.prepend(header);"
      "VP37.page={kind:'compare'};VP37.put('','catalog',{runs:{},iterations:[]}"
      ");VP37.start();"
      "for(let i=0;i<8;i++)await Promise.resolve();"
      "check(document.querySelector('#app "
      "h1').textContent===(step===1?'Porównanie A/B':'A/B "
      "comparison'),'offline body language after reload');"
      "check(location.hash==='#a=&b=&basis=filtered','comparison fragment "
      "retained');"
      "if(step===2){document.getElementById('result').textContent='PASS "
      "'+checks;return;}"
      "const next=new "
      "URL(location.href);next.searchParams.set('lang-step',step+1);next."
      "searchParams.set('checks',checks);"
      "history.replaceState(null,'',next);"
      "if(step===1)history.replaceState=()=>{throw Error('History disabled');};"
      "document.querySelector('.language-"
      "switch').click();"
      "setTimeout(()=>document.getElementById('result').textContent='FAIL "
      "offline language did not reload',200);return;}"
      "const "
      "ui=VP37.live.createLive(document.getElementById('app'),catalog);"
      "const m=ui.model;fixture.forEach(x=>m.receive(x));ui.render();\n",
      out);
  check(out,
        "ui.charts.length===19 && "
        "ui.charts.every(c=>c.plot.layers.every(l=>l.y.some(Number.isFinite)))",
        "all panels populated");
  check(
      out,
      "m.fields.get('ADJ.d').unit==='Hz' && m.fields.get('C.pdt').unit==='us'",
      "catalog units");
  check(out,
        "ui.charts[0].plot.layers[2].y[1]===1095 && "
        "ui.charts[6].plot.layers[0].y[0]===2.1",
        "independent numeric fixture");
  check(out,
        "ui.charts[18].plot.layers[0].y.join()==='1,0' && "
        "ui.charts[18].plot.layers[2].y.join()==='1,0'",
        "status bits");
  check(out,
        "document.documentElement.lang==='en' && "
        "document.getElementById('live-pause').textContent==='Pause' && "
        "ui.charts[0].plot.layers[0].name==='Requested target'",
        "English default and short labels");
  check(out,
        "catalog.every(f=>f.label && f.label_pl && f.description && "
        "f.description_pl) && "
        "catalog.find(f=>f.kind==='ADJ'&&f.key==='v').unit==='dV' && "
        "ui.charts[18].plot.layers[0].name==='Signal lost' && "
        "ui.charts[18].plot.layers[4].name==='ADJ.s[4]'",
        "complete bilingual catalog and known status bits");
  check(
      out,
      "ui.charts[0].plot.layers.length===3 && ui.charts[0].plot.y1<2000 && "
      "ui.charts[2].plot.layers[0].y[0]===33079 && "
      "ui.charts[3].keys.join()==='pwm,ff' && ui.charts[4].keys.includes('lo')",
      "frequency and PID scales separated");
  fputs("const legend=ui.charts[0].plot.legend.firstElementChild;"
        "legend.open=true;const "
        "focus=legend.querySelector('summary');focus.focus();"
        "for(let i=0;i<10;i++)ui.render();",
        out);
  check(
      out,
      "legend===ui.charts[0].plot.legend.firstElementChild && legend.open && "
      "document.activeElement===focus && focus.title.includes('C.tar [Hz]') && "
      "legend.textContent.includes('Position target')",
      "legend identity focus and accessible description survive samples");
  fputs("ui.charts[0].plot.opts.onCursor(0.025);", out);
  check(out,
        "ui.charts.every(c=>c.plot.cursor===0.025) && "
        "document.querySelector('.live-cursor').textContent.includes('Measured "
        "position')",
        "common cursor");
  fputs(
      "document.getElementById('live-pause').click();const "
      "frozen=m.csv();const saved=m.view();\n"
      "for(let i=1;i<=9000;i++){for(const kind of ['C','IPULSE','CFG','ADJ'])"
      "m.record({kind,t:101+i/20,values:{adj:i,us:2000000+i*50000,unknown:i}});"
      "m.record({kind:'TEST',t:101+i/20,text:'x'.repeat(300)});}\n",
      out);
  fputs("ui.setStatus('Connected');VP37.i18n.setLanguage('pl');", out);
  check(out,
        "m.csv()===frozen && m.snapshot && legend.open && "
        "document.activeElement===focus && "
        "ui.charts[0].plot.layers[0].name==='Cel zadany' && "
        "document.getElementById('live-pause').textContent==='Wznów live' && "
        "ui.status.textContent==='Połączono' && "
        "document.documentElement.lang==='pl'",
        "Polish switch preserves paused data focus and details");
  fputs("VP37.i18n.setLanguage('en');", out);
  check(out, "m.csv()===frozen && m.view().rows.C[1].values.adj===1095",
        "pause survives eviction");
  check(out,
        "m.buffers.C.rows().length===4096 && "
        "m.buffers.IPULSE.rows().length===4096 && "
        "m.buffers.CFG.rows().length===512 && "
        "m.buffers.ADJ.rows().length===512 && m.events.rows().length===256",
        "bounded rings and events");
  check(out,
        "!Object.hasOwn(m.buffers.C.rows()[0].values,'unknown') && "
        "m.events.rows().every(e=>e.text.length<=256)",
        "unknown keys and text bounded");
  fputs("ui.charts[0].plot.opts.onZoom(0.02,0.05);ui.render();", out);
  check(out,
        "ui.charts.every(c=>c.plot.x0===0.02 && Math.abs(c.plot.x1-0.05)<1e-8)",
        "shared zoom");
  fputs("const "
        "zoomCSV=m.csv();VP37.i18n.setLanguage('pl');VP37.i18n.setLanguage('en'"
        ");",
        out);
  check(out,
        "m.csv()===zoomCSV && m.range[0]===0.02 && "
        "Math.abs(m.range[1]-0.05)<1e-8",
        "language switch preserves zoom and CSV wire keys");
  check(out,
        "!m.csv().includes(',1080,') && m.csv().includes(',1095,') && "
        "m.csv().split('\\r\\n').slice(1,-1).every(row=>Number(row.split(',')["
        "0])>=0.02 && Number(row.split(',')[0])<=0.05)",
        "CSV matches visible range");
  check(out,
        "ui.png().toDataURL('image/png').startsWith('data:image/"
        "png;base64,iVBOR')",
        "PNG encoded from canvas");
  fputs("const fill=CanvasRenderingContext2D.prototype.fillText;let clipped=0;"
        "CanvasRenderingContext2D.prototype.fillText=function(text,x,y){"
        "if(this.font==='14px system-ui' && "
        "x+this.measureText(text).width>this.canvas.width)clipped++;"
        "return fill.call(this,text,x,y);};"
        "document.getElementById('app').style.width='380px';"
        "VP37.i18n.setLanguage('pl');ui.png();VP37.i18n.setLanguage('en');ui."
        "png();"
        "CanvasRenderingContext2D.prototype.fillText=fill;"
        "document.getElementById('app').style.width='';ui.render();",
        out);
  check(out, "clipped===0",
        "PNG wraps English and Polish names in narrow views");
  check(out,
        "VP37.i18n.diagnostic('okno analizy z a/b.json')==='analysis window "
        "from a/b.json' && "
        "VP37.i18n.diagnostic('nieprzypisane: plik nie ma pola "
        "source')==='unassigned: no source field'",
        "generated diagnostics translated with source path preserved");
  fputs("document.getElementById('live-reset').click();ui.render();", out);
  check(out, "m.view().a===saved.a && m.view().b===saved.b",
        "reset paused zoom");
  fputs("m.resume();m.disconnect();m.receive({type:'hello',protocol:1,epoch:1,"
        "next:0});"
        "m.receive({type:'samples',epoch:1,seq:0,records:[{kind:'C',t:0,values:"
        "{us:10,adj:7}}]});"
        "let rows=m.buffers.C.rows();",
        out);
  check(out,
        "rows[rows.length-1].segment!==rows[rows.length-2].segment && "
        "rows[rows.length-1].t>rows[rows.length-2].t",
        "restart reusing epoch splits time");
  fputs("const count=m.buffers.C.rows().length;const last=m.latest;"
        "m.receive({type:'samples',epoch:1,seq:0,records:[{kind:'C',t:0,values:"
        "{adj:99}}]});",
        out);
  check(out, "m.buffers.C.rows().length===count && m.latest===last",
        "duplicate batch ignored");
  fputs("m.receive({type:'samples',epoch:1,seq:4,records:[{kind:'C',t:0.1,"
        "values:{us:20,adj:8}}]});ui.render();",
        out);
  check(out, "m.events.rows().some(e=>e.text.includes('Stream gap'))",
        "sequence gap visible");
  fputs("const "
        "epochSegment=m.segment;m.receive({type:'reset',protocol:1,epoch:2,"
        "next:5});"
        "m.receive({type:'samples',epoch:2,seq:5,records:[{kind:'C',t:0,values:"
        "{us:100,adj:9}}]});",
        out);
  check(out,
        "m.segment>epochSegment && m.buffers.C.rows().at(-1).values.adj===9",
        "log replacement resets segment");
  fputs("const "
        "restartSegment=m.segment;m.record({kind:'C',t:0.01,values:{us:10,adj:"
        "10}});",
        out);
  check(out, "m.segment>restartSegment", "MCU restart splits segment");
  // Observe actual canvas path commands; disconnected samples must use moveTo.
  fputs("let moves=0,lines=0;const "
        "g={beginPath(){},moveTo(){moves++;},lineTo(){lines++;},stroke(){}};"
        "ui.charts[0].plot.drawLayer(g,{kind:'line',x:[0,1,2,3,9],y:[1,2,3,"
        "null,5],segment:[1,1,2,2,2],maxGap:2});",
        out);
  check(out, "moves===3 && lines===1",
        "canvas never joins segments or missing data");
  fputs("m.record({kind:'T',t:0.005,values:{us:5000,adj:101}});"
        "m.record({kind:'T',t:0.01,values:{us:10000,adj:102}});"
        "m.record({kind:'trace',t:500,text:'end samples:2'});ui.render();",
        out);
  check(out,
        "m.selected.complete && m.view().a===0 && m.view().b===0.005 && "
        "m.view().rows.C[0].source===0.005 && "
        "m.csv().includes('\"T\"')",
        "trace uses MCU samples not dump time");
  fputs("const chosen=m.selected;const chosenCSV=m.csv();"
        "const "
        "storage=Storage.prototype.setItem;Storage.prototype.setItem=()=>{"
        "throw Error('blocked');};"
        "VP37.i18n.setLanguage('pl');VP37.i18n.setLanguage('en');Storage."
        "prototype.setItem=storage;",
        out);
  check(out,
        "m.selected===chosen && m.csv()===chosenCSV && "
        "ui.charts[0].plot.layers[2].description.includes('T.adj') && "
        "VP37.i18n.href('index.html').includes('lang=en')",
        "trace and navigation survive blocked storage");
  check(out, "ui.charts.filter(c=>!c.panel.hidden).every(c=>c.kind==='C')",
        "separate trace view");
  fputs("const traceCSV=m.csv();for(let i=0;i<10;i++){"
        "m.record({kind:'T',t:i+1,values:{us:1000000+i,adj:i}});"
        "m.record({kind:'trace',t:500,text:'end samples:2'});}",
        out);
  check(out,
        "m.traces.length===4 && m.traces.every(t=>!t.complete) && "
        "m.csv()===traceCSV",
        "partial traces bounded without replacing paused view");
  fputs("m.record({kind:'T',t:20,values:{us:20,adj:1}});m.disconnect();", out);
  check(out,
        "!m.traces.at(-1).complete && "
        "m.traces.at(-1).reason.includes('Disconnected')",
        "interrupted trace retained incomplete");
  fputs("for(let "
        "i=0;i<2100;i++)m.record({kind:'T',t:30+i/"
        "1000,values:{us:30000+i,adj:i}});"
        "m.record({kind:'trace',t:500,text:'end samples:2100'});",
        out);
  check(out, "m.traces.at(-1).rows.length===2048 && !m.traces.at(-1).complete",
        "trace sample cap");
  fputs("m.record({kind:'T',t:40,values:{adj:1}});m.record({kind:'trace',t:500,"
        "text:'end samples:1'});",
        out);
  check(out,
        "!m.traces.at(-1).complete && m.traces.at(-1).reason.includes('us')",
        "trace without MCU time incomplete");
  check(out, "m.buffers.C.rows().at(-1).values.adj===10",
        "trace never enters live control buffer");
  fputs("const old=new VP37.live.LiveModel(catalog);old.receive(fixture[0]);"
        "old.record({kind:'C',t:2,values:{adj:2}});"
        "const absent=VP37.live.layerFor(old,old.view(),'C','pdt',0);",
        out);
  check(out, "absent.name.includes('no data') && absent.y.every(v=>v===null)",
        "old revision missing fields explicit");
  // Static pages still use the original loader and renderer without live.js
  // state.
  fputs("VP37.put('offline','part',{C:{adj:[3]}});", out);
  check(out,
        "typeof VP37.start==='function' && typeof VP37.markdown==='function'",
        "offline entry points preserved");
  fputs("ui.destroy();const failures=[];"
        "const "
        "originalCheck=check;check=(ok,label)=>{checks++;if(!ok)failures.push("
        "label);};"
        "const regression=new VP37.live.LiveModel(catalog);"
        "regression.record({kind:'C',t:100,values:{us:2400000000,adj:1}});"
        "regression.record({kind:'C',t:101,restart:true,values:{us:3000000,adj:"
        "2}});"
        "regression.record({kind:'ADJ',t:101.01,values:{d:2}});"
        "regression.record({kind:'=1+1',t:101,text:'formula'});",
        out);
  check(out, "regression.segment===1 && regression.view().rows.ADJ.length===1",
        "server clock restart starts a visible segment");
  check(out,
        "regression.events.rows().every(e=>Number.isFinite(e.source)&&Number."
        "isInteger(e.segment))",
        "events retain source time and segment");
  check(out, "regression.csv().includes(\"' =1\".replace(' ',''))",
        "CSV neutralizes formula event kinds");
  fputs("regression.record({kind:'T',t:200,values:{us:10,adj:1}});"
        "regression.record({kind:'trace:',t:201,text:'HAL_OK samples:2'});"
        "regression.record({kind:'T',t:202,values:{us:20,adj:2}});"
        "regression.record({kind:'T',t:202.005,values:{us:5020,adj:3}});"
        "regression.record({kind:'trace',t:203,text:'end samples:2'});",
        out);
  check(out,
        "regression.traces.length===2 && !regression.traces[0].complete && "
        "regression.traces[1].complete",
        "trace without end is isolated from the next capture");
  fputs(
      "const "
      "epochUI=VP37.live.createLive(document.getElementById('app'),catalog);"
      "epochUI.model.record({kind:'T',t:1794510000.001,values:{us:1,adj:1}});"
      "epochUI.model.record({kind:'T',t:1794510000.006,values:{us:5001,adj:2}})"
      ";"
      "epochUI.model.record({kind:'trace',t:1794510001,text:'end samples:2'});"
      "const labels=[];const "
      "ctx=epochUI.charts[0].plot.canvas.getContext('2d');const "
      "tickFill=ctx.fillText;"
      "ctx.fillText=function(text,x,y){if(y>100)labels.push(text);return "
      "tickFill.call(this,text,x,y);};"
      "epochUI.render();ctx.fillText=tickFill;",
      out);
  check(out, "new Set(labels).size>=3",
        "millisecond trace has distinct time ticks");
  fputs(
      "epochUI.destroy();const realFetch=window.fetch,realWS=window.WebSocket;"
      "const sockets=[];window.fetch=async "
      "path=>({ok:true,json:async()=>path==='/api/"
      "fields'?catalog:{ws_port:12345}});"
      "window.WebSocket=class{constructor(){sockets.push(this);}close(){this."
      "closed=true;}};"
      "await VP37.startLive();for(let i=0;i<8;i++)await Promise.resolve();"
      "window.dispatchEvent(new "
      "PageTransitionEvent('pagehide',{persisted:true}));"
      "window.dispatchEvent(new "
      "PageTransitionEvent('pageshow',{persisted:true}));"
      "for(let i=0;i<8;i++)await Promise.resolve();",
      out);
  check(out,
        "document.querySelectorAll('#app canvas').length===19 && "
        "document.querySelectorAll('.language-switch').length===1 && "
        "sockets.length===2 && sockets[0].closed",
        "cached live page resumes charts language and connection");
  fputs("window.dispatchEvent(new "
        "PageTransitionEvent('pagehide',{persisted:false}));"
        "window.fetch=realFetch;window.WebSocket=realWS;check=originalCheck;"
        "if(failures.length)throw Error(failures.join('; '));"
        "const next=new "
        "URL(location.href);next.search='?lang-step=0&lang=en&checks='+checks;"
        "next.hash='a=&b=&basis=filtered';location.assign(next);"
        "}catch(error){document.getElementById('result').textContent='FAIL "
        "'+error.stack;}})();"
        "</script>",
        out);
  ok = !ferror(out) && ok;
  return fclose(out) == 0 && ok;
}
