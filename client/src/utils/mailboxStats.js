const StatsLib = {
  avg: (arr) => arr.length ? (arr.reduce((a, b) => a + b, 0) / arr.length) : 0,
  max: (arr) => arr.length ? Math.max(...arr) : 0,
  min: (arr) => arr.length ? Math.min(...arr) : 0,
};

export const calculateColumnStats = (records) => {
  if (!records?.length) return {
    temp: { avg: 0, max: 0, min: 0 },
    rssi: { avg: 0, max: 0, min: 0 },
    lastTime: "--:--",
    lastTemp: "--",
    lastRSSI: "--",
    lastTrigger: "NONE"
  };

  const temps = records.map(r => parseFloat(r.temp)).filter(v => !isNaN(v));
  const rssis = records.map(r => parseInt(r.rssi)).filter(v => !isNaN(v));
  const last = records[0];
  const firstTrigger = records.find(r => r.triggerEvent === 'open' || r.triggerEvent === 'reset');
  const status = firstTrigger ? firstTrigger.triggerEvent : 'NONE';

  return {
    temp: { avg: StatsLib.avg(temps).toFixed(1), max: StatsLib.max(temps), min: StatsLib.min(temps) },
    rssi: { avg: StatsLib.avg(rssis).toFixed(0), max: StatsLib.max(rssis), min: StatsLib.min(rssis) },
    lastTime: new Date(last.datetime).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit', hour12: true }),
    lastTemp: last.temp,
    lastRSSI: last.rssi,
    lastTrigger: last.triggerEvent,
    status: status
  };
};