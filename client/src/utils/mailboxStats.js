const StatsLib = {
  avg: (arr) => arr.length ? (arr.reduce((a, b) => a + b, 0) / arr.length) : 0,
  max: (arr) => arr.length ? Math.max(...arr) : 0,
  min: (arr) => arr.length ? Math.min(...arr) : 0,
};

export const calculateMailboxStats = (records) => {
  if (!records?.length) return null;

  const temps = records.map(r => parseFloat(r.temp)).filter(v => !isNaN(v));
  const rssis = records.map(r => parseInt(r.rssi)).filter(v => !isNaN(v));
  const last = records[0];

  return {
    temp: { avg: StatsLib.avg(temps).toFixed(1), max: StatsLib.max(temps), min: StatsLib.min(temps) },
    rssi: { avg: StatsLib.avg(rssis).toFixed(0), max: StatsLib.max(rssis), min: StatsLib.min(rssis) },
    lastTime: new Date(last.datetime).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' }),
    lastTemp: last.temp,
    lastRSSI: last.rssi,
    lastTrigger: last.triggerEvent === 1 ? "MAIL" : "HEARTBEAT"
  };
};