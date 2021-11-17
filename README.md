#ddump
Dump discord channel messages and guilds to text/CSV.

###Building
- Install cmake, libcurl
- `mkdir build && cd build && cmake ../ && make`

###Usage
```
ddump [OPTIONS]...
-t [TOKEN] - Use [TOKEN] for authentication
(ddump also reads from the DDUMP_TOKEN environment variable, this option overrides that)
-c [CHANNEL_ID] - Dump the channel with the id [CHANNEL_ID]
-g [GUILD_ID] - Dump the guild with the id [GUILD_ID] and all its channels
(this overrides any provided channel passed with -c)
-d - Download all attachments
-h - Show this help
```