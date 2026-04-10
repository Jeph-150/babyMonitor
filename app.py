from flask import Flask, request, jsonify, render_template_string
from datetime import datetime
import threading

app = Flask(__name__)

# Latest data store
data = {
    "temp": 0.0,
    "bpm": 0,
    "motion": 0.0,
    "finger": 0,
    "spo2": 0.0,
    "snd": 0,
    "last_update": "No data yet"
}
data_lock = threading.Lock()

HTML = """
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Baby Monitor</title>
<link href="https://fonts.googleapis.com/css2?family=DM+Mono:wght@300;400;500&family=Sora:wght@300;400;600&display=swap" rel="stylesheet">
<style>
  :root {
    --bg: #0a0e14;
    --surface: #111820;
    --card: #151d27;
    --border: #1e2d3d;
    --accent: #4fc3f7;
    --accent2: #81d4fa;
    --green: #69f0ae;
    --amber: #ffd740;
    --red: #ff5252;
    --text: #cdd9e5;
    --muted: #4a6278;
    --font-display: 'Sora', sans-serif;
    --font-mono: 'DM Mono', monospace;
  }

  * { margin: 0; padding: 0; box-sizing: border-box; }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: var(--font-display);
    min-height: 100vh;
    padding: 2rem;
  }

  /* Subtle grid background */
  body::before {
    content: '';
    position: fixed;
    inset: 0;
    background-image:
      linear-gradient(var(--border) 1px, transparent 1px),
      linear-gradient(90deg, var(--border) 1px, transparent 1px);
    background-size: 40px 40px;
    opacity: 0.3;
    pointer-events: none;
    z-index: 0;
  }

  .container { position: relative; z-index: 1; max-width: 900px; margin: 0 auto; }

  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 2.5rem;
    padding-bottom: 1.5rem;
    border-bottom: 1px solid var(--border);
  }

  .logo {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .logo-icon {
    width: 36px; height: 36px;
    border-radius: 10px;
    background: linear-gradient(135deg, var(--accent), #1565c0);
    display: flex; align-items: center; justify-content: center;
    font-size: 18px;
  }

  h1 {
    font-size: 1.25rem;
    font-weight: 600;
    letter-spacing: -0.02em;
    color: #e8f4fd;
  }

  .subtitle {
    font-size: 0.72rem;
    color: var(--muted);
    font-family: var(--font-mono);
    letter-spacing: 0.08em;
    text-transform: uppercase;
  }

  .status-bar {
    display: flex;
    align-items: center;
    gap: 8px;
    font-family: var(--font-mono);
    font-size: 0.72rem;
    color: var(--muted);
  }

  .dot {
    width: 7px; height: 7px;
    border-radius: 50%;
    background: var(--muted);
    transition: background 0.3s;
  }
  .dot.live { background: var(--green); box-shadow: 0 0 6px var(--green); animation: pulse 2s infinite; }

  @keyframes pulse {
    0%, 100% { opacity: 1; } 50% { opacity: 0.5; }
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 1rem;
    margin-bottom: 1rem;
  }

  .card {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: 14px;
    padding: 1.4rem 1.4rem 1.2rem;
    position: relative;
    overflow: hidden;
    transition: border-color 0.3s, transform 0.2s;
  }

  .card:hover {
    border-color: #2a3f55;
    transform: translateY(-1px);
  }

  /* Accent line on left */
  .card::before {
    content: '';
    position: absolute;
    left: 0; top: 20%; bottom: 20%;
    width: 2px;
    border-radius: 2px;
    background: var(--card-color, var(--accent));
    opacity: 0.7;
  }

  .card-label {
    font-family: var(--font-mono);
    font-size: 0.65rem;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    color: var(--muted);
    margin-bottom: 0.6rem;
  }

  .card-value {
    font-family: var(--font-mono);
    font-size: 2.2rem;
    font-weight: 500;
    color: var(--card-color, var(--accent));
    letter-spacing: -0.03em;
    line-height: 1;
    transition: color 0.3s;
  }

  .card-unit {
    font-size: 0.85rem;
    font-weight: 300;
    color: var(--muted);
    margin-left: 3px;
  }

  .card-status {
    margin-top: 0.6rem;
    font-size: 0.7rem;
    font-family: var(--font-mono);
    color: var(--muted);
  }

  .badge {
    display: inline-block;
    padding: 2px 8px;
    border-radius: 4px;
    font-size: 0.65rem;
    font-family: var(--font-mono);
    font-weight: 500;
    letter-spacing: 0.06em;
    text-transform: uppercase;
  }

  .badge-ok    { background: rgba(105,240,174,0.12); color: var(--green); }
  .badge-warn  { background: rgba(255,215,64,0.12);  color: var(--amber); }
  .badge-alert { background: rgba(255,82,82,0.12);   color: var(--red);   }
  .badge-off   { background: rgba(74,98,120,0.2);    color: var(--muted); }

  /* Wide card for sound */
  .card-wide {
    grid-column: span 2;
  }

  .sound-bar-wrap {
    margin-top: 0.8rem;
    height: 6px;
    background: var(--border);
    border-radius: 3px;
    overflow: hidden;
  }

  .sound-bar {
    height: 100%;
    border-radius: 3px;
    background: linear-gradient(90deg, var(--green), var(--amber), var(--red));
    transition: width 0.4s ease;
  }

  .footer {
    margin-top: 1.5rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-family: var(--font-mono);
    font-size: 0.68rem;
    color: var(--muted);
    padding-top: 1rem;
    border-top: 1px solid var(--border);
  }

  .blink { animation: blink 1s step-end infinite; }
  @keyframes blink { 50% { opacity: 0; } }

  /* Highlight flash on update */
  @keyframes flash {
    0% { background: rgba(79,195,247,0.08); }
    100% { background: var(--card); }
  }
  .flash { animation: flash 0.6s ease-out; }
</style>
</head>
<body>
<div class="container">
  <header>
    <div class="logo">
      <div class="logo-icon">👶</div>
      <div>
        <h1>Baby Monitor</h1>
        <div class="subtitle">Live vitals dashboard</div>
      </div>
    </div>
    <div class="status-bar">
      <div class="dot" id="live-dot"></div>
      <span id="last-update">Waiting for data...</span>
    </div>
  </header>

  <div class="grid">
    <!-- Body Temp -->
    <div class="card" id="card-temp" style="--card-color: #4fc3f7">
      <div class="card-label">Body temp</div>
      <div class="card-value" id="val-temp">--<span class="card-unit">°C</span></div>
      <div class="card-status" id="badge-temp"></div>
    </div>

    <!-- Heart Rate -->
    <div class="card" id="card-bpm" style="--card-color: #ff5252">
      <div class="card-label">Heart rate</div>
      <div class="card-value" id="val-bpm">--<span class="card-unit">bpm</span></div>
      <div class="card-status" id="badge-bpm"></div>
    </div>

    <!-- SpO2 -->
    <div class="card" id="card-spo2" style="--card-color: #69f0ae">
      <div class="card-label">SpO₂</div>
      <div class="card-value" id="val-spo2">--<span class="card-unit">%</span></div>
      <div class="card-status" id="badge-spo2"></div>
    </div>

    <!-- Motion -->
    <div class="card" id="card-motion" style="--card-color: #ffd740">
      <div class="card-label">Motion</div>
      <div class="card-value" id="val-motion">--<span class="card-unit">m/s²</span></div>
      <div class="card-status" id="badge-motion"></div>
    </div>

    <!-- Finger -->
    <div class="card" id="card-finger" style="--card-color: #ce93d8">
      <div class="card-label">Sensor contact</div>
      <div class="card-value" id="val-finger" style="font-size:1.4rem; padding-top:0.3rem">--</div>
      <div class="card-status" id="badge-finger"></div>
    </div>

    <!-- Sound (wide) -->
    <div class="card card-wide" id="card-snd" style="--card-color: #80cbc4">
      <div class="card-label">Sound level</div>
      <div class="card-value" id="val-snd">--<span class="card-unit">ADC</span></div>
      <div class="sound-bar-wrap">
        <div class="sound-bar" id="sound-bar" style="width:0%"></div>
      </div>
      <div class="card-status" id="badge-snd" style="margin-top:0.5rem"></div>
    </div>
  </div>

  <div class="footer">
    <span>ESP32 → Flask → Browser</span>
    <span><span class="blink">_</span></span>
    <span>Auto-refresh every 1s</span>
  </div>
</div>

<script>
async function refresh() {
  try {
    const res = await fetch('/latest');
    const d = await res.json();

    // Helper: flash card on update
    function flash(id) {
      const el = document.getElementById(id);
      el.classList.remove('flash');
      void el.offsetWidth;
      el.classList.add('flash');
    }

    function badge(cls, text) {
      return `<span class="badge badge-${cls}">${text}</span>`;
    }

    // Live dot
    const isLive = d.last_update !== 'No data yet';
    document.getElementById('live-dot').className = 'dot' + (isLive ? ' live' : '');
    document.getElementById('last-update').textContent = d.last_update;

    // Body temp
    const t = parseFloat(d.temp);
    document.getElementById('val-temp').innerHTML = t.toFixed(1) + '<span class="card-unit">°C</span>';
    const tempBadge = t < 36.0 ? badge('warn','Low') : t > 37.5 ? badge('alert','High') : badge('ok','Normal');
    document.getElementById('badge-temp').innerHTML = tempBadge;
    flash('card-temp');

    // BPM
    const bpm = parseInt(d.bpm);
    document.getElementById('val-bpm').innerHTML = (bpm > 0 ? bpm : '--') + '<span class="card-unit">bpm</span>';
    const bpmBadge = bpm === 0 ? badge('off','No reading') : bpm < 100 || bpm > 160 ? badge('warn','Check') : badge('ok','Normal');
    document.getElementById('badge-bpm').innerHTML = bpmBadge;
    flash('card-bpm');

    // SpO2
    const spo2 = parseFloat(d.spo2);
    document.getElementById('val-spo2').innerHTML = (spo2 > 0 ? spo2.toFixed(1) : '--') + '<span class="card-unit">%</span>';
    const spo2Badge = spo2 === 0 ? badge('off','No reading') : spo2 < 90 ? badge('alert','Low') : badge('ok','Normal');
    document.getElementById('badge-spo2').innerHTML = spo2Badge;
    flash('card-spo2');

    // Motion
    const mot = parseFloat(d.motion);
    document.getElementById('val-motion').innerHTML = mot.toFixed(2) + '<span class="card-unit">m/s²</span>';
    const motBadge = mot > 2.0 ? badge('warn','Active') : badge('ok','Calm');
    document.getElementById('badge-motion').innerHTML = motBadge;
    flash('card-motion');

    // Finger
    const fng = parseInt(d.finger);
    document.getElementById('val-finger').innerHTML = fng ? '● Contact' : '○ No contact';
    document.getElementById('badge-finger').innerHTML = fng ? badge('ok','Sensor on') : badge('off','Place sensor');
    flash('card-finger');

    // Sound
    const snd = parseInt(d.snd);
    document.getElementById('val-snd').innerHTML = snd + '<span class="card-unit">ADC</span>';
    const sndPct = Math.min(100, (snd / 4095) * 100).toFixed(1);
    document.getElementById('sound-bar').style.width = sndPct + '%';
    const sndBadge = snd > 2000 ? badge('alert','Crying!') : snd > 500 ? badge('warn','Noise') : badge('ok','Quiet');
    document.getElementById('badge-snd').innerHTML = sndBadge;
    flash('card-snd');

  } catch(e) {
    document.getElementById('last-update').textContent = 'Connection lost...';
  }
}

setInterval(refresh, 1000);
refresh();
</script>
</body>
</html>
"""

@app.route('/')
def index():
    return render_template_string(HTML)

@app.route('/data', methods=['POST'])
def receive_data():
    with data_lock:
        payload = request.get_json(force=True)
        data.update(payload)
        data['last_update'] = datetime.now().strftime('%H:%M:%S')
    return jsonify({"status": "ok"})

@app.route('/latest')
def latest():
    with data_lock:
        return jsonify(data)

if __name__ == '__main__':
    # 0.0.0.0 makes it reachable from ESP32 on the same network
    print("Baby Monitor Dashboard running at http://localhost:5000")
    app.run(host='0.0.0.0', port=5000, debug=False)