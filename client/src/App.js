import React, { useState, useMemo, useEffect } from 'react';
import { useMailboxData } from './hooks/useMailboxData';
import MailboxSidebar from './components/sidebar/mailboxSidebar';
import MailboxTable from './components/MailboxTable/MailboxTable';
import ControlBar from './components/ControlBar/ControlBar';
import { calculateColumnStats  } from './utils/mailboxStats';
import './App.css';

function App() {
  const [selectedHours, setSelectedHours] = useState(24);
  const [isSidebarOpen, setSidebarOpen] = useState(true);
  
  const { mailboxRecords, isLoading } = useMailboxData(selectedHours);

  const columnStats = useMemo(() => calculateColumnStats (mailboxRecords), [mailboxRecords]);

  const [serverTime, setServerTime] = useState("00:00 AM");

  const [latestTrigger, setLatestTrigger] = useState("NONE");
  const [latestTriggerDate, setLatestTriggerDate] = useState("");

  const cl1pClick = async () => {
    try{
      const response = await fetch(`${process.env.REACT_APP_MAILBOX_API_URL}/api/cl1p`, {method: 'POST', });
      if (response.status === 204) {
          console.log("Action acknowledged by server.");
      }
      else {
        console.error("Server returned an error");
      }
    }
    catch (error) {
      console.error("Fetch error:", error);
    }
  };

  const updateTime = () => {
    fetch(`${process.env.REACT_APP_MAILBOX_API_URL}/api/time`)
      .then(res => res.json())
      .then(data => setServerTime(data.time))
      .catch(err => console.error("Time fetch failed", err));
  };

  const fetchLatestPersistentTrigger = () => {
    fetch(`${process.env.REACT_APP_MAILBOX_API_URL}/api/latest-trigger`)
      .then(res => res.json())
      .then(data => {
        setLatestTrigger(data.latestTrigger);
        setLatestTriggerDate(data.latestTriggerDate);
      })
      .catch(err => console.error("Latest trigger fetch failed", err));
  };

  useEffect(() => {
    const updateBoth = () => {
      updateTime();
      fetchLatestPersistentTrigger();
    };

    updateBoth();
    const interval = setInterval(updateBoth, 5000);
    return () => clearInterval(interval);
  }, []);

  if (isLoading) return <div className="loader">Loading Mailbox Box Data...</div>;

  return (
    <div className="App">
      <ControlBar
        selectedHours={selectedHours}
        onHoursChange={setSelectedHours}
        columnStats={columnStats}
        cl1pClick={cl1pClick}
        records={mailboxRecords}
        toggleSidebar={() => setSidebarOpen(!isSidebarOpen)}
        isSidebarOpen={isSidebarOpen}
        serverTime={serverTime}
        latestTrigger={latestTrigger}        
        latestTriggerDate={latestTriggerDate}
      />
      <main>
        <MailboxSidebar 
          isOpen={isSidebarOpen} 
          records={mailboxRecords}
          selectedHours={selectedHours} 
        />
        <MailboxTable 
          records={mailboxRecords} 
          columnStats={columnStats} 
        />
      </main>
    </div>
  );
}

export default App;
