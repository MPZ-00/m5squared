/*
 * Embedded HTML for M25 Virtual Joystick
 * This file contains the web interface as a C string constant
 */

#ifndef INDEX_HTML_H
#define INDEX_HTML_H

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>M25 Virtual Joystick</title>
<style>
* {margin: 0;padding: 0;box-sizing: border-box;touch-action: none;}body {font-family: Arial, sans-serif;background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);color: white;display: flex;flex-direction: column;align-items: center;justify-content: center;min-height: 100vh;padding: 20px;}h1 {font-size: 24px;margin-bottom: 10px;}.status-bar {display: flex;gap: 10px;margin-bottom: 20px;}.status {padding: 10px 20px;background: rgba(255,255,255,0.2);border-radius: 20px;font-size: 14px;}.status.connected {background: rgba(76, 175, 80, 0.8);}.status.disconnected {background: rgba(244, 67, 54, 0.8);}.joystick-container {position: relative;width: 300px;height: 300px;background: rgba(255,255,255,0.1);border: 3px solid rgba(255,255,255,0.3);border-radius: 50%;margin: 20px;box-shadow: 0 10px 40px rgba(0,0,0,0.3);}.joystick-base {position: absolute;top: 50%;left: 50%;width: 80%;height: 80%;transform: translate(-50%, -50%);border: 2px dashed rgba(255,255,255,0.3);border-radius: 50%;}.joystick-stick {position: absolute;top: 50%;left: 50%;width: 80px;height: 80px;background: radial-gradient(circle, #ffffff, #cccccc);border: 3px solid #888;border-radius: 50%;transform: translate(-50%, -50%);box-shadow: 0 5px 20px rgba(0,0,0,0.5);cursor: pointer;transition: box-shadow 0.1s;}.joystick-stick.active {box-shadow: 0 5px 30px rgba(255,255,255,0.6);border-color: #4CAF50;}.coordinates {margin-top: 20px;font-size: 16px;font-family: monospace;}.instructions {margin-top: 20px;text-align: center;font-size: 12px;opacity: 0.8;max-width: 300px;}.emergency-stop {margin-top: 20px;padding: 15px 40px;font-size: 18px;font-weight: bold;background: #f44336;color: white;border: none;border-radius: 10px;cursor: pointer;box-shadow: 0 5px 15px rgba(0,0,0,0.3);}.emergency-stop:active {transform: scale(0.95);}
</style>
</head>
<body>
<h1>M25 Virtual Joystick</h1>
<div class="status-bar">
<div class="status" id="wsStatus">WebSocket: Connecting...</div>
<div class="status" id="bleStatus">BLE: Disconnected</div>
</div>
<div class="joystick-container" id="joystickContainer">
<div class="joystick-base"></div>
<div class="joystick-stick" id="joystickStick"></div>
</div>
<div class="coordinates">
X: <span id="xValue">0.00</span> | Y: <span id="yValue">0.00</span>
</div>
<button class="emergency-stop" onclick="emergencyStop()">EMERGENCY STOP</button>
<div class="instructions">
Touch and drag the joystick to control the wheelchair.
Release to stop. The emergency stop button will immediately halt all movement.
</div>
<script>const ws=new WebSocket('ws://'+window.location.hostname+':81');const stick=document.getElementById('joystickStick');const container=document.getElementById('joystickContainer');const xValue=document.getElementById('xValue');const yValue=document.getElementById('yValue');const wsStatus=document.getElementById('wsStatus');const bleStatus=document.getElementById('bleStatus');let isDragging=!1;let centerX=0;let centerY=0;let maxRadius=0;function updateCenter(){const rect=container.getBoundingClientRect();centerX=rect.width/2;centerY=rect.height/2;maxRadius=(rect.width/2)*0.7}
updateCenter();window.addEventListener('resize',updateCenter);ws.onopen=function(){wsStatus.textContent='WebSocket: Connected';wsStatus.className='status connected'};ws.onclose=function(){wsStatus.textContent='WebSocket: Disconnected';wsStatus.className='status disconnected'};ws.onerror=function(){wsStatus.textContent='WebSocket: Error';wsStatus.className='status disconnected'};ws.onmessage=function(event){try{const data=JSON.parse(event.data);if(data.bleStatus!==undefined){if(data.bleStatus==='connected'){bleStatus.textContent='BLE: Connected';bleStatus.className='status connected'}else{bleStatus.textContent='BLE: Disconnected';bleStatus.className='status disconnected'}}}catch(e){}};function sendJoystickData(x,y,active){if(ws.readyState===WebSocket.OPEN){const data=JSON.stringify({x:x.toFixed(3),y:y.toFixed(3),active:active});ws.send(data)}}
function updateStickPosition(x,y){const dx=x-centerX;const dy=y-centerY;const distance=Math.sqrt(dx*dx+dy*dy);let finalX=dx;let finalY=dy;if(distance>maxRadius){const angle=Math.atan2(dy,dx);finalX=Math.cos(angle)*maxRadius;finalY=Math.sin(angle)*maxRadius}
stick.style.transform=`translate(calc(-50% + ${finalX}px), calc(-50% + ${finalY}px))`;const normalizedX=finalX/maxRadius;const normalizedY=-finalY/maxRadius;xValue.textContent=normalizedX.toFixed(2);yValue.textContent=normalizedY.toFixed(2);return{x:normalizedX,y:normalizedY}}
function resetStick(){stick.style.transform='translate(-50%, -50%)';stick.classList.remove('active');xValue.textContent='0.00';yValue.textContent='0.00';sendJoystickData(0,0,!1)}
stick.addEventListener('touchstart',function(e){e.preventDefault();isDragging=!0;stick.classList.add('active')});container.addEventListener('touchmove',function(e){if(!isDragging)return;e.preventDefault();const touch=e.touches[0];const rect=container.getBoundingClientRect();const x=touch.clientX-rect.left;const y=touch.clientY-rect.top;const pos=updateStickPosition(x,y);sendJoystickData(pos.x,pos.y,!0)});document.addEventListener('touchend',function(e){if(isDragging){isDragging=!1;resetStick()}});stick.addEventListener('mousedown',function(e){e.preventDefault();isDragging=!0;stick.classList.add('active')});container.addEventListener('mousemove',function(e){if(!isDragging)return;e.preventDefault();const rect=container.getBoundingClientRect();const x=e.clientX-rect.left;const y=e.clientY-rect.top;const pos=updateStickPosition(x,y);sendJoystickData(pos.x,pos.y,!0)});document.addEventListener('mouseup',function(e){if(isDragging){isDragging=!1;resetStick()}});function emergencyStop(){sendJoystickData(0,0,!1);resetStick();ws.send(JSON.stringify({command:'emergencyStop'}))}</script>
</body>
</html>
)rawliteral";

#endif // INDEX_HTML_H
