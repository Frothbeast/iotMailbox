import { useState, useEffect } from 'react';

export function useMailboxData(hours) {
    const [mailboxRecords, setMailboxRecords] = useState([]);
    const [isLoading, setIsLoading] = useState(true);

    useEffect(() => {
        let interval;

        const fetchData = () => {
            fetch(`${process.env.REACT_APP_MAILBOX_API_URL}/api/mailboxData?hours=${hours}`)
                .then(res => res.json())
                .then(data => {
                    if (Array.isArray(data)) {
                        const mappedData = data.map(r => ({
                            datetime: r.datetime,
                            // Mapping to names expected by calculateMailboxStats and MailboxTable
                            temp: r.temp, 
                            rssi: r.rssi,
                            triggerEvent: r.triggerEvent,
                            // Retaining original fields if needed for other logic
                            readingCount: r.readingCount
                        }));
                        setMailboxRecords(mappedData);
                    }
                    setIsLoading(false);
                })
                .catch(err => {
                    console.error("Fetch error:", err);
                    setIsLoading(false);
                });
        };

        const setupInterval = () => {
            if (interval) clearInterval(interval);
            
            // Poll at 1s if visible, 60s if hidden to save server resources
            const pollRate = document.visibilityState === 'visible' ? 1000 : 60000;
            interval = setInterval(fetchData, pollRate);
        };

        fetchData();
        setupInterval();

        const handleVisibilityChange = () => {
            setupInterval();
        };

        document.addEventListener('visibilitychange', handleVisibilityChange);

        return () => {
            clearInterval(interval);
            document.removeEventListener('visibilitychange', handleVisibilityChange);
        };
    }, [hours]);

    return { mailboxRecords, isLoading };
}