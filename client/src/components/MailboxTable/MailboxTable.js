import React from 'react';

const MailboxTable = ({ records, stats }) => {
  if (!stats) return null;
  return (
    <div className="tableContainer">
      <table className="heaterTable">
        <thead>
          <tr>
            <th>Time</th>
            <th>Trigger</th>
            <th>Temp °C</th>
            <th>RSSI</th>
          </tr>
        </thead>
        <tbody>
          {records.map((r, i) => (
            <tr key={i}>
              <td>{new Date(r.datetime).toLocaleString()}</td>
              <td>{r.triggerEvent === 1 ? "Mail" : "Check"}</td>
              <td>{r.temp}</td>
              <td>-{r.rssi} dBm</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
};

export default MailboxTable;