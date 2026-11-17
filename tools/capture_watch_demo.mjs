#!/usr/bin/env node
// Dependency-free Chrome capture. MP4 encoding uses macOS AVFoundation.
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { readFile, writeFile, mkdir, mkdtemp, rm, stat } from 'node:fs/promises';
import { dirname, extname, join, resolve, sep } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: {
  root: { type: 'string', default: 'web/watch-demo' },
  url: { type: 'string' },
  output: { type: 'string', default: '/tmp/hacktor-watch-demo-frames' },
  duration: { type: 'string', default: '12' },
  start: { type: 'string', default: '0' },
  fps: { type: 'string', default: '30' },
  width: { type: 'string', default: '1080' },
  height: { type: 'string', default: '1080' },
  chrome: { type: 'string', default: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' },
  mp4: { type: 'string' },
  webm: { type: 'string' },
  background: { type: 'string', default: '0f151a' },
  probe: { type: 'boolean', default: false },
  check: { type: 'boolean', default: false },
  help: { type: 'boolean', default: false },
} });
if (values.help) {
  console.log(`Usage: node tools/capture_watch_demo.mjs [--root web/watch-demo | --url URL]
  --output FRAME_DIRECTORY --duration 12 --start 0 --fps 30
  [--mp4 OUTPUT.mp4] [--webm OUTPUT.webm] [--background 0f151a]
  [--width 1080 --height 1080] [--chrome PATH] [--probe] [--check]

The page must expose window.watchDemo with ready (Promise), pause(),
renderAt(seconds, {mode:'fixed'}), and canvas (or canvas#watch-canvas).
PNG frames are deterministic. MP4 uses the included Swift/macOS encoder.
WebM is recorded in real time from the same canvas with Chrome MediaRecorder.
Use --probe to report browser encoder support without loading a demo.`);
  process.exit(0);
}
const duration = Number(values.duration);
const start = Number(values.start);
const fps = Number(values.fps);
if (!(duration > 0 && Number.isFinite(start) && Number.isInteger(fps) && fps > 0 && fps <= 120)) {
  throw new Error('--duration must be positive; --start must be finite; --fps must be an integer from 1 to 120.');
}
const delay = (ms) => new Promise((done) => setTimeout(done, ms));
const root = resolve(values.root);
const output = resolve(values.output);
let server;
let chrome;
let cdp;
const profile = await mkdtemp(join(tmpdir(), 'hacktor-watch-chrome-'));
let chromeOutput = '';
const pending = new Map();
let nextId = 0;
const errors = [];

async function startServer() {
  const mime = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
    '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml',
    '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.webp': 'image/webp', '.mp4': 'video/mp4', '.webm': 'video/webm' };
  server = createServer(async (request, response) => {
    try {
      const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
      let target = resolve(root, '.' + pathname);
      if (target !== root && !target.startsWith(root + sep)) throw new Error('Outside document root');
      if ((await stat(target)).isDirectory()) target = join(target, 'index.html');
      const bytes = await readFile(target);
      response.writeHead(200, { 'Content-Type': mime[extname(target)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
      response.end(bytes);
    } catch {
      response.writeHead(404);
      response.end('Not found');
    }
  });
  await new Promise((done, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', done); });
  return `http://127.0.0.1:${server.address().port}/`;
}

function send(method, params = {}, timeoutMs = 60000) {
  const id = ++nextId;
  return new Promise((done, reject) => {
    const timeout = setTimeout(() => { pending.delete(id); reject(new Error(`CDP timeout: ${method}`)); }, timeoutMs);
    pending.set(id, { done, reject, timeout });
    cdp.send(JSON.stringify({ id, method, params }));
  });
}

async function evaluate(expression, timeoutMs = 60000) {
  const result = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }, timeoutMs);
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
  return result.result.value;
}

async function runProcess(command, args) {
  const child = spawn(command, args, { stdio: 'inherit' });
  await new Promise((done, reject) => {
    child.once('error', reject);
    child.once('exit', (code) => code === 0 ? done() : reject(new Error(`${command} exited ${code}`)));
  });
}

try {
  const url = values.probe ? 'about:blank' : values.url || await startServer();
  chrome = spawn(values.chrome, [
    '--headless=new', '--no-sandbox', '--disable-gpu', '--mute-audio', '--hide-scrollbars',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--disable-backgrounding-occluded-windows', '--force-device-scale-factor=1',
    '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank',
  ], { stdio: ['ignore', 'ignore', 'pipe'] });
  chrome.stderr.on('data', (data) => { if (chromeOutput.length < 30000) chromeOutput += data; });
  let port;
  for (let attempt = 0; attempt < 150; ++attempt) {
    if (chrome.exitCode !== null) throw new Error(`Chrome exited ${chrome.exitCode}: ${chromeOutput}`);
    try { port = Number((await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]); break; }
    catch { await delay(100); }
  }
  if (!port) throw new Error(`Chrome did not expose DevTools: ${chromeOutput}`);
  const target = await (await fetch(`http://127.0.0.1:${port}/json/new?about:blank`, { method: 'PUT' })).json();
  cdp = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((done, reject) => { cdp.addEventListener('open', done, { once: true }); cdp.addEventListener('error', reject, { once: true }); });
  cdp.addEventListener('message', ({ data }) => {
    const message = JSON.parse(data);
    if (message.id) {
      const request = pending.get(message.id);
      if (!request) return;
      pending.delete(message.id);
      clearTimeout(request.timeout);
      if (message.error) request.reject(new Error(message.error.message)); else request.done(message.result);
    } else if (message.method === 'Runtime.exceptionThrown') {
      errors.push(message.params.exceptionDetails.exception?.description || message.params.exceptionDetails.text);
    }
  });
  await send('Page.enable');
  await send('Runtime.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: Number(values.width), height: Number(values.height), deviceScaleFactor: 1, mobile: false });
  if (values.probe) {
    console.log(JSON.stringify(await evaluate(`({ userAgent:navigator.userAgent, mediaRecorder:typeof MediaRecorder,
      mp4:MediaRecorder.isTypeSupported('video/mp4;codecs=avc1.42001E'),
      webmVP9:MediaRecorder.isTypeSupported('video/webm;codecs=vp9'),
      webmVP8:MediaRecorder.isTypeSupported('video/webm;codecs=vp8'),
      videoEncoder:typeof VideoEncoder })`), null, 2));
  } else {
    await send('Page.navigate', { url });
    let ready = false;
    for (let attempt = 0; attempt < 150; ++attempt) {
      if (await evaluate('Boolean(window.watchDemo && window.watchDemo.renderAt)')) { ready = true; break; }
      await delay(100);
    }
    if (!ready) throw new Error('Page did not expose watchDemo.renderAt(). ' + errors.join('\n'));
    await evaluate(`(async()=>{await window.watchDemo.ready;await document.fonts.ready;window.watchDemo.pause?.();return true;})()`);
    const frameCount = Math.round(duration * fps);
    await mkdir(output, { recursive: true });
    if (values.check) {
      // File URLs have opaque origins in Chrome. Embed the local firmware
      // reference so direct-from-disk standalone checks remain origin-clean.
      const referenceURL = 'data:image/png;base64,' +
        (await readFile(join(root, 'assets/digital-reference.png'))).toString('base64');
      const checks = await evaluate(`(async()=>{
        const demo=window.watchDemo;
        demo.pause();
        const paused=!demo.isPlaying;
        demo.play();const started=demo.isPlaying;
        demo.pause();const stopped=!demo.isPlaying;
        const button=document.getElementById('playback-toggle');
        button.click();const buttonStarted=demo.isPlaying;
        button.click();const buttonStopped=!demo.isPlaying;
        const fixed=demo.renderAt(7,{mode:'fixed'});
        const fixedDate=new Date(fixed.time);
        const fixedClock=fixedDate.getFullYear()===2026&&fixedDate.getMonth()===10&&fixedDate.getDate()===7&&fixedDate.getHours()===10&&fixedDate.getMinutes()===8;
        const live=demo.renderAt(7,{mode:'live'});
        const liveClock=Math.abs(new Date(live.time).getTime()-Date.now())<1500;
        demo.setSample({steps:1957,battery:79,charging:false});
        const rendered=demo.renderFace('digital',new Date(2026,10,7,10,8,30));
        const actual=rendered.getContext('2d').getImageData(0,0,240,240).data;
        const reference=new Image();reference.src=${JSON.stringify(referenceURL)};await reference.decode();
        const referenceCanvas=document.createElement('canvas');referenceCanvas.width=referenceCanvas.height=240;
        const referenceContext=referenceCanvas.getContext('2d');referenceContext.drawImage(reference,0,0);
        const expected=referenceContext.getImageData(0,0,240,240).data;
        const difference=referenceContext.createImageData(240,240);
        let rgb565PixelDifferences=0,maxChannelError=0,inkShapeDifferences=0;
        let minX=240,minY=240,maxX=-1,maxY=-1;
        const samples=[];
        for(let pixel=0;pixel<240*240;pixel++){
          const i=pixel*4;
          const q=(v,j)=>v[j]>>3<<11|v[j+1]>>2<<5|v[j+2]>>3;
          const delta=Math.max(Math.abs(actual[i]-expected[i]),Math.abs(actual[i+1]-expected[i+1]),Math.abs(actual[i+2]-expected[i+2]));
          maxChannelError=Math.max(maxChannelError,delta);
          if(Boolean(actual[i]||actual[i+1]||actual[i+2])!==Boolean(expected[i]||expected[i+1]||expected[i+2]))inkShapeDifferences++;
          if(q(actual,i)!==q(expected,i)){
            rgb565PixelDifferences++;
            const x=pixel%240,y=Math.floor(pixel/240);
            minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);
            difference.data[i]=255;difference.data[i+3]=255;
            if(samples.length<12)samples.push({x,y,actual:[...actual.slice(i,i+3)],expected:[...expected.slice(i,i+3)]});
          }
        }
        const digitalPNG=rendered.toDataURL('image/png').split(',')[1];
        referenceContext.putImageData(difference,0,0);
        const differencePNG=referenceCanvas.toDataURL('image/png').split(',')[1];
        const transition=demo.renderAt(5.4,{mode:'fixed'});
        demo.pause();
        return{paused,started,stopped,buttonStarted,buttonStopped,fixedClock,liveClock,
          transitionVisible:transition.transitioning===true,rgb565PixelDifferences,maxChannelError,inkShapeDifferences,
          differenceBounds:maxX<0?null:{minX,minY,maxX,maxY},differenceSamples:samples,digitalPNG,differencePNG};
      })()`);
      await writeFile(join(output, 'digital-browser.png'), Buffer.from(checks.digitalPNG, 'base64'));
      await writeFile(join(output, 'digital-difference.png'), Buffer.from(checks.differencePNG, 'base64'));
      delete checks.digitalPNG;delete checks.differencePNG;
      await evaluate('window.watchDemo.play();true');
      await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'reduce' }] });
      await delay(100);
      checks.reducedMotionPauses = await evaluate('!window.watchDemo.isPlaying');
      await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'no-preference' }] });
      await evaluate('window.watchDemo.pause();true');
      checks.errors = [...errors];
      await writeFile(join(output, 'browser-checks.json'), JSON.stringify(checks, null, 2) + '\n');
      console.log('Browser checks: ' + JSON.stringify(checks));
      for (const key of ['paused','started','stopped','buttonStarted','buttonStopped','fixedClock','liveClock','transitionVisible','reducedMotionPauses']) {
        if (!checks[key]) throw new Error('Browser behavior check failed: ' + key);
      }
    }
    // Remove only frames generated by this tool, keeping unrelated output files.
    const { readdir } = await import('node:fs/promises');
    for (const file of await readdir(output)) if (/^frame-\d{5}\.png$/.test(file)) await rm(join(output, file));
    console.log(`Capturing ${frameCount} frames from ${url}`);
    for (let frame = 0; frame < frameCount; ++frame) {
      const data = await evaluate(`(async()=>{
        await window.watchDemo.renderAt(${start + frame / fps},{mode:'fixed'});
        const canvas=window.watchDemo.canvas||document.getElementById('watch-canvas');
        if(!canvas)throw new Error('Missing watch demo canvas');
        return canvas.toDataURL('image/png').split(',')[1];
      })()`);
      await writeFile(join(output, `frame-${String(frame).padStart(5, '0')}.png`), Buffer.from(data, 'base64'));
      if (frame % fps === 0) console.log(`  ${frame / fps}s / ${duration}s`);
    }
    const dimensions = await evaluate(`(()=>{const c=window.watchDemo.canvas||document.getElementById('watch-canvas');return{width:c.width,height:c.height};})()`);
    await writeFile(join(output, 'capture.json'), JSON.stringify({ ...dimensions, fps, start, duration, frameCount, errors }, null, 2) + '\n');
    if (values.mp4) {
      const encoder = join(dirname(fileURLToPath(import.meta.url)), 'encode_watch_demo.swift');
      const cache = join(tmpdir(), 'hacktor-watch-swift-module-cache');
      await mkdir(cache, { recursive: true });
      await runProcess('/usr/bin/swift', ['-module-cache-path', cache, encoder, '--input', output, '--output', resolve(values.mp4), '--fps', String(fps), '--background', values.background]);
    }
    if (values.webm) {
      console.log(`Recording ${duration}s WebM…`);
      const result = await evaluate(`(async()=>{
        window.watchDemo.pause?.();
        const canvas=window.watchDemo.canvas||document.getElementById('watch-canvas');
        const mime=['video/webm;codecs=vp9','video/webm;codecs=vp8','video/webm'].find(t=>MediaRecorder.isTypeSupported(t));
        if(!mime)throw new Error('WebM recording is not supported');
        await window.watchDemo.renderAt(${start},{mode:'fixed'});
        const stream=canvas.captureStream(${fps});
        const recorder=new MediaRecorder(stream,{mimeType:mime,videoBitsPerSecond:3000000});
        const chunks=[];recorder.ondataavailable=e=>{if(e.data.size)chunks.push(e.data);};
        const ended=new Promise((resolve,reject)=>{recorder.onstop=resolve;recorder.onerror=reject;});
        recorder.start();
        const start=performance.now();
        while(performance.now()-start<${duration * 1000}){
          await window.watchDemo.renderAt(${start}+(performance.now()-start)/1000,{mode:'fixed'});
          await new Promise(requestAnimationFrame);
        }
        recorder.stop();await ended;stream.getTracks().forEach(t=>t.stop());
        const bytes=new Uint8Array(await new Blob(chunks,{type:mime}).arrayBuffer());
        let binary='';for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));
        return{base64:btoa(binary),mime,bytes:bytes.length};
      })()`, duration * 1000 + 30000);
      const destination = resolve(values.webm);
      await mkdir(dirname(destination), { recursive: true });
      await writeFile(destination, Buffer.from(result.base64, 'base64'));
      console.log(`${result.mime}, ${result.bytes} bytes → ${destination}`);
    }
    if (errors.length) throw new Error('Browser exceptions: ' + errors.join('\n'));
    console.log(`Saved ${frameCount} PNG frames (${dimensions.width}×${dimensions.height}).`);
  }
} finally {
  for (const request of pending.values()) { clearTimeout(request.timeout); request.reject(new Error('Capture closed')); }
  pending.clear();
  cdp?.close();
  if (chrome && chrome.exitCode === null) {
    chrome.kill('SIGTERM');
    await Promise.race([new Promise((done) => chrome.once('exit', done)), delay(3000)]);
    if (chrome.exitCode === null) chrome.kill('SIGKILL');
  }
  if (server) await new Promise((done) => server.close(done));
  await rm(profile, { recursive: true, force: true }).catch(() => {});
}
