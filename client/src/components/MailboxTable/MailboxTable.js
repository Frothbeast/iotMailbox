import React from 'react';
import './MailboxTable.css';

const MailboxTable = ({ records, columnStats }) => {
  return (
    <div className="tableContainer">
      <table className="mailboxTable">
        <thead>
          <tr>
            <th>Time</th>
            <th>Trigger</th>
            <th>Temp °C</th>
            <th>RSSI</th>
          </tr>
        </thead>
        <tbody className="mailboxTableBody">
          <tr className="mailboxTablePlaceholder"></tr>
          {Array.isArray(records) && records.length > 0 ? (
            records.map((record, i) => (
              <tr key={record.id || i} className="mailboxTableRow">
                <td className="mailboxTableCell">
                  {record.datetime 
                    ? new Date(record.datetime).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit', hour12: true }) 
                    : "N/a"}
                </td>
                <td className="mailboxTableCell">
                  {record.triggerEvent}
                </td>
                <td className="mailboxTableCell">{record.temp ?? "N/a"}</td>
                <td className="mailboxTableCell">
                  {record.rssi ? `-${record.rssi} dBm` : "N/a"}
                </td>
              </tr>
            ))
          ) : (
            <tr><td colSpan="4" style={{textAlign: 'center'}}>No data for selected period</td></tr>
          )}
        </tbody>
      </table>
    </div>
  );
};

export default MailboxTable;