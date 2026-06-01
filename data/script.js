// Simple Canvas-based Temperature Graph (no external dependencies)
let tempCanvas = null;
let tempCtx = null;
let graphData = {
    times: [],
    measured: [],
    target: [],
    pidOutput: []
};

const MAX_POINTS = 60; // Store up to 60 points for 30 seconds at 500ms interval
const MAX_LOG_LINES = 120;
const GRAPH_PADDING = 40;
const GRAPH_MIN_TEMP_RANGE = 80;
const PID_MAX_OUTPUT = 100;

let graphWidthCss = 500;
let graphHeightCss = 240;

function appendWifiLog(text) {
    const logNode = document.getElementById('wifiLog');
    if (!logNode) {
        return;
    }

    const lines = logNode.textContent ? logNode.textContent.split('\n') : [];
    lines.push(text);
    while (lines.length > MAX_LOG_LINES) {
        lines.shift();
    }

    logNode.textContent = lines.join('\n');
    logNode.scrollTop = logNode.scrollHeight;
}

function drawGraph() {
    if (!tempCtx) return;
    
    const canvas = tempCanvas;
    const ctx = tempCtx;
    const w = graphWidthCss;
    const h = graphHeightCss;
    
    // Clear canvas
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, w, h);
    
    // Draw grid and axes
    ctx.strokeStyle = '#e0e0e0';
    ctx.lineWidth = 1;
    ctx.font = '12px Arial';
    ctx.fillStyle = '#666';
    
    const tempValues = graphData.measured.concat(graphData.target).filter(v => Number.isFinite(v));
    let minTemp = 0;
    let maxTemp = 300;

    if (tempValues.length > 0) {
        const dataMin = Math.min.apply(null, tempValues);
        const dataMax = Math.max.apply(null, tempValues);
        const center = (dataMin + dataMax) / 2;
        const halfRange = Math.max((dataMax - dataMin) / 2 + 10, GRAPH_MIN_TEMP_RANGE / 2);
        minTemp = Math.max(0, Math.floor(center - halfRange));
        maxTemp = Math.min(350, Math.ceil(center + halfRange));
        if (maxTemp - minTemp < GRAPH_MIN_TEMP_RANGE) {
            maxTemp = Math.min(350, minTemp + GRAPH_MIN_TEMP_RANGE);
        }
    }

    const tempRange = Math.max(1, maxTemp - minTemp);
    const plotHeight = h - 2 * GRAPH_PADDING;
    const tempStep = tempRange <= 100 ? 10 : 20;

    // Horizontal grid lines (temperatures)
    for (let i = minTemp; i <= maxTemp; i += tempStep) {
        const y = h - GRAPH_PADDING - ((i - minTemp) / tempRange) * plotHeight;
        ctx.beginPath();
        ctx.moveTo(GRAPH_PADDING, y);
        ctx.lineTo(w - 10, y);
        ctx.stroke();
        ctx.fillText(i + ' C', 5, y + 5);
    }
    
    // Y-axis (temperature)
    ctx.strokeStyle = '#000';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(GRAPH_PADDING, GRAPH_PADDING);
    ctx.lineTo(GRAPH_PADDING, h - GRAPH_PADDING);
    ctx.stroke();
    
    // X-axis (time)
    ctx.beginPath();
    ctx.moveTo(GRAPH_PADDING, h - GRAPH_PADDING);
    ctx.lineTo(w - 10, h - GRAPH_PADDING);
    ctx.stroke();
    
    if (graphData.measured.length === 0) return;
    
    const numPoints = graphData.measured.length;
    const xStep = (w - GRAPH_PADDING - 10) / (MAX_POINTS - 1);
    
    // Draw Measured Temperature (blue line)
    ctx.strokeStyle = '#0088ff';
    ctx.lineWidth = 2.4;
    ctx.beginPath();
    for (let i = 0; i < numPoints; i++) {
        const x = GRAPH_PADDING + i * xStep;
        const y = h - GRAPH_PADDING - ((graphData.measured[i] - minTemp) / tempRange) * plotHeight;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();
    
    // Draw Target Temperature (red line)
    ctx.strokeStyle = '#ff0000';
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let i = 0; i < numPoints; i++) {
        const x = GRAPH_PADDING + i * xStep;
        const y = h - GRAPH_PADDING - ((graphData.target[i] - minTemp) / tempRange) * plotHeight;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();
    
    // Draw PID output on right axis 0..100
    ctx.strokeStyle = '#ff9900';
    ctx.lineWidth = 1;
    ctx.setLineDash([5, 5]);
    ctx.beginPath();
    for (let i = 0; i < numPoints; i++) {
        const x = GRAPH_PADDING + i * xStep;
        const y = h - GRAPH_PADDING - (graphData.pidOutput[i] / PID_MAX_OUTPUT) * plotHeight;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();
    ctx.setLineDash([]);

    // Right Y-axis marks for PID
    ctx.fillStyle = '#666';
    ctx.font = '11px Arial';
    for (let p = 0; p <= PID_MAX_OUTPUT; p += 20) {
        const y = h - GRAPH_PADDING - (p / PID_MAX_OUTPUT) * plotHeight;
        ctx.fillText(String(p) + '%', w - 34, y + 4);
    }
    
    // Draw legend
    ctx.font = '12px Arial';
    ctx.fillStyle = '#0088ff';
    ctx.fillRect(w - 150, 10, 12, 12);
    ctx.fillStyle = '#000';
    ctx.fillText('Measured Temp', w - 135, 20);
    
    ctx.fillStyle = '#ff0000';
    ctx.fillRect(w - 150, 30, 12, 12);
    ctx.fillStyle = '#000';
    ctx.fillText('Target Temp', w - 135, 40);
    
    ctx.fillStyle = '#ff9900';
    ctx.fillRect(w - 150, 50, 12, 12);
    ctx.fillStyle = '#000';
    ctx.fillText('PID Output', w - 135, 60);
}

setInterval(reloadTemp, 500);  // time in milliseconds
//-------------------------------------------------------
function reloadTemp()           // update the temperature every 1s
{
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function()
    {
        if(this.readyState == 4 && this.status == 200)
        {
          var obj = JSON.parse(this.responseText);
          document.getElementById('varMesT').innerHTML = obj.varMesT;
          document.getElementById('varCurrT').innerHTML = obj.varCurrT;
          document.getElementById('varComputePID').innerHTML = obj.varComputePID;
          
          // Update graph data
          updateTempGraph(parseFloat(obj.varMesT), parseFloat(obj.varCurrT), parseFloat(obj.varComputePID));
        }
    };
    xhr.open('GET', '/tempurl', true);
    xhr.send();
}

function updateTempGraph(measured, target, pidOutput) {
    graphData.measured.push(measured);
    graphData.target.push(target);
    graphData.pidOutput.push(pidOutput);
    
    // Keep maximum data points
    if (graphData.measured.length > MAX_POINTS) {
        graphData.measured.shift();
        graphData.target.shift();
        graphData.pidOutput.shift();
    }
    
    drawGraph();
}

//--------------------- Status --------------------------
setInterval(reloadStatus, 1000); // time in milliseconds
function reloadStatus()          // update debug
{
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function()
    {
        if(this.readyState == 4 && this.status == 200)
        {   // parse JSON text
          var obj = JSON.parse(this.responseText);
          document.getElementById('varMode').innerHTML = obj.varMode;
          document.getElementById('varStatus').innerHTML = obj.varStatus;
                    updateModeButtons(obj.varMode);
        }
    };
    xhr.open('GET', '/statusurl', true);
    xhr.send();
}
//-------------------- debuger ------------------------
setInterval(ComputeDrebugreload, 250); // time in milliseconds
function ComputeDrebugreload()          // update debug
{
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function()
    {
        if(this.readyState == 4 && this.status == 200)
        {   // parse JSON text
          var obj = JSON.parse(this.responseText);
          document.getElementById('vardeltaTemp').innerHTML = obj.vardeltaTemp;
          document.getElementById('vartProg').innerHTML = obj.vartProg;
          document.getElementById('varcoeffTempTable').innerHTML = obj.varcoeffTempTable;
          document.getElementById('varsteepness').innerHTML = obj.varsteepness;
          document.getElementById('vargetPower').innerHTML = obj.vargetPower;
        }
    };
    xhr.open('GET', '/gdbvarurl', true);
    xhr.send();
}
//-------------------------------------------------------
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

window.addEventListener('load', onLoad);

function onLoad(event) {
    initGraph();
    initWebSocket();
    initButton();
}

function initGraph() {
    tempCanvas = document.getElementById('tempChart');
    if (tempCanvas) {
        tempCtx = tempCanvas.getContext('2d');
        resizeGraphCanvas();
        window.addEventListener('resize', resizeGraphCanvas);
        drawGraph();
    }
}

function resizeGraphCanvas() {
    if (!tempCanvas || !tempCtx) {
        return;
    }

    const rect = tempCanvas.getBoundingClientRect();
    graphWidthCss = Math.max(320, Math.floor(rect.width || 500));
    graphHeightCss = Math.max(220, Math.floor(rect.height || 240));

    const dpr = window.devicePixelRatio || 1;
    tempCanvas.width = Math.floor(graphWidthCss * dpr);
    tempCanvas.height = Math.floor(graphHeightCss * dpr);
    tempCtx.setTransform(dpr, 0, 0, dpr, 0, 0);
    tempCtx.imageSmoothingEnabled = true;
}

// ----------------------------------------------------------------------------
// WebSocket handling
// ----------------------------------------------------------------------------

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    let payload;
    try {
        payload = JSON.parse(event.data);
    } catch (e) {
        const led = document.getElementById('led');
        if (led) {
            led.className = event.data;
        }
        return;
    }

    if (payload.type === 'log') {
        appendWifiLog(payload.message);
        return;
    }

    if (payload.varStatus) {
        const statusNode = document.getElementById('varStatus');
        if (statusNode) {
            statusNode.textContent = payload.varStatus;
        }
    }

    if (payload.varMode) {
        const modeNode = document.getElementById('varMode');
        if (modeNode) {
            modeNode.textContent = payload.varMode;
        }
        updateModeButtons(payload.varMode);
    }

    if (payload.vgetPower !== undefined) {
        const powerNode = document.getElementById('vargetPower');
        if (powerNode) {
            powerNode.textContent = payload.vgetPower;
        }
    }
}

// ----------------------------------------------------------------------------
// Button handling
// ----------------------------------------------------------------------------

function initButton() {
    bindModeButton('modeStandbay', 0);
    bindModeButton('modeSnPb', 2);
    bindModeButton('modePbFree', 3);
    bindModeButton('modeManual', 4);
}

function bindModeButton(id, startValue) {
    const button = document.getElementById(id);
    if (!button) {
        return;
    }

    button.addEventListener('click', function () {
        sendModeCommand(startValue);
    });
}

function sendModeCommand(startValue) {
    if (!websocket || websocket.readyState !== WebSocket.OPEN) {
        return;
    }

    websocket.send(JSON.stringify({ Start: startValue }));
}

function updateModeButtons(modeText) {
    const modeStr = String(modeText || '');

    const modeMap = {
        modeStandbay: modeStr.indexOf('FLAG_STANDBAY') !== -1,
        modeManual: modeStr.indexOf('MANUAL') !== -1,
        modeSnPb: modeStr.indexOf('FLAG_SnPb') !== -1,
        modePbFree: modeStr.indexOf('FLAG_PbFree') !== -1
    };

    Object.keys(modeMap).forEach(function (id) {
        const button = document.getElementById(id);
        if (!button) {
            return;
        }

        if (modeMap[id]) {
            button.classList.add('is-active');
        } else {
            button.classList.remove('is-active');
        }
    });
}