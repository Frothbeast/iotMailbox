import React from 'react';
import HeaterChart from '../HeaterTable/HeaterChart'; // Reuse existing chart component

const MailboxControlBar = ({ records, stats, onHoursChange }) => {
  return (
    <header className="controlBar">
      <div className="brandSection">
        <div className="brand">Mailbox</div>
      </div>

      <div className="centerSection">
        <div className="lastRun">
          <span className="label">Last Event</span>
          <span className="value">{stats?.lastTrigger}</span>
          <span className="unit">{stats?.lastTime}</span>
        </div>
        <div className="lastTemp">
          <span className="label">Outside Temp</span>
          <span className="value">{stats?.lastTemp}</span>
          <span className="unit">°C</span>
        </div>
        <select onChange={(e) => onHoursChange(e.target.value)} className="myBUTTon">
          <option value="24">24 Hours</option>
          <option value="168">7 Days</option>
        </select>
      </div>

      <div className="chartSection">
        <div className="chartContainer">
           <div className="chartWatermark">TEMP</div>
           <HeaterChart 
              labels={records.map((_, i) => i)}
              datasets={[{ label: "Temp", color: "orange", data: records.map(r => r.temp) }]}
              options={{ responsive: true, maintainAspectRatio: false, plugins: { legend: { display: false } }, scales: { x: { display: false }, y: { display: false } } }}
           />
        </div>
      </div>
    </header>
  );
};

export default MailboxControlBar;