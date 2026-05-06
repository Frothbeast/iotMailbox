import React from 'react';
import MailboxChart from '../MailboxTable/MailboxChart'; // Reuse existing chart component
import './ControlBar.css';

const ControlBar = ({ records, columnStats, onHoursChange, clipClick, serverTime, toggleSidebar, cl1pClick, isSidebarOpen }) => {
  return (
    <header className="controlBar">
      <div className="brandSection">
        <div className="brand">Mailbox</div>
        <div className="serverTime">
          <span className="stLabel">Server Time:</span>
          <span>{serverTime ?? "00:00:00"}</span>
        </div>
      </div>

      <div className="centerSection">
        <div className="lastRun">
          <span className="label">Last Event</span>
          <span className="value">{columnStats?.lastTrigger}</span>
          <span className="unit">{columnStats?.lastTime}</span>
        </div>
        <div className="lastTemp">
          <span className="label">Outside Temp</span>
          <span className="value">{columnStats?.lastTemp}</span>
          <span className="unit">°C</span>
        </div>
        <div className="buttonRow">
          <button className="sidebarButton myBUTTon" onClick={toggleSidebar}>
              {isSidebarOpen ? "Table" : "Graph"}
          </button>
          <button onClick={cl1pClick} className="cl1pButton myBUTTon">CL1P</button>
          <select 
            className="selectedHours myBUTTon" 
            value={selectedHours} 
            onChange={(e) => onHoursChange(Number(e.target.value))}
          >
            <option value={1}>1 Hour</option>
            <option value={8}>8 Hour</option>
            <option value={24}>24 Hour</option>
            <option value={168}>168 Hour</option>
          </select>
        </div>
      </div>

      <div className="chartSection">
        <div className="chartContainer">
           <div className="chartWatermark">TEMP</div>
           <MailboxChart 
              labels={records.map((_, i) => i)}
              datasets={[{ label: "Temp", color: "orange", data: records.map(r => r.temp) }]}
              options={{ responsive: true, maintainAspectRatio: false, plugins: { legend: { display: false } }, scales: { x: { display: false }, y: { display: false } } }}
           />
        </div>
      </div>
    </header>
  );
};

export default ControlBar;