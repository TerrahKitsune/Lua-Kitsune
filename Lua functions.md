# Lua Environment API Reference

A comprehensive reference for all available functions in the Lua environment.

---

## Table of Contents

- [Global Functions](#global-functions)
- [Console](#console)
- [Mutex](#mutex)
- [Macro](#macro)
- [Redis](#redis)
- [CSV](#csv)
- [Kafka](#kafka)
- [FileAsync](#fileasync)
- [FTP](#ftp)
- [Sound](#sound)
- [ODBC](#odbc)
- [Archive](#archive)
- [Stream](#stream)
- [TTS](#tts)
- [Env](#env)
- [Zip](#zip)
- [Server](#server)
- [Client](#client)
- [Pipe](#pipe)
- [Base64](#base64)
- [Socket](#socket)
- [Services](#services)
- [Aes](#aes)
- [Process](#process)
- [Imgui](#imgui)
- [HTTP](#http)
- [Hashing (SHA256, MD5, SHA1)](#hashing)
- [MySQL](#mysql)
- [Timer](#timer)
- [SQLite](#sqlite)
- [Image](#image)
- [Json](#json)
- [Wchar](#wchar)
- [FileSystem](#filesystem)
- [TWODA (2da)](#twoda)
- [TLK](#tlk)
- [KeyBif](#keybif)
- [ERF](#erf)
- [GFF](#gff)
---

## Global Functions

### UUID Generation

```lua
string, raw16bytestring UUID()
```
Returns a UUID from Windows `CoCreateGuid`. Returns `nil` if uniqueness cannot be guaranteed.

### CRC Functions

```lua
int CRC32(stringdata, opt existingcrc)
int CRC64(data)
```
- `CRC32`: Calculate a CRC32 checksum
- `CRC64`: Calculate a CRC64 (works with stream and wchar, converts non-strings via `tostring`)

### Time & Sleep

```lua
nil Sleep(opt int)
int Time()
ms Runtime()
```
- `Sleep`: Sleep for 1 millisecond (default) or specified ms (max 1000)
- `Time`: Get current Unix epoch in milliseconds
- `Runtime`: Get runtime in milliseconds

### Error Handling

```lua
string, code GetLastError(opt lasterrorcode)
```
Retrieves the last error code as a message and code.

### Console Output

```lua
nil CLS()
background, foreground GetTextColor()
nil SetTextColor(background, foreground)
int GetKey()
bool HasKeyDown()
nil Put(text)
bool ShellExecute(file, parameter)
```

### Ticker Function

```lua
nil SetTicker(function, opt ms)
```
Sets a function as a ticker. If the parameter isn't a function, the ticker is disabled.

### Memory

```lua
int GetMemory()
```
Returns memory in bytes used by Lua.

### Application Control

```lua
Exit(opt code)
```
Called when GFF.exe shuts down; can also be called to shut down prematurely.

### Registry Access

```lua
string GetRegistryValue(key, subkey, entry)
-- or on failure:
nil, errorMessage GetRegistryValue(key, subkey, entry)
```

**Key constants:**
| Value | Registry Key |
|-------|--------------|
| 0 | HKEY_LOCAL_MACHINE |
| 1 | HKEY_CLASSES_ROOT |
| 2 | HKEY_CURRENT_CONFIG |
| 3 | HKEY_CURRENT_USER |
| 4 | HKEY_PERFORMANCE_DATA |
| 5 | HKEY_PERFORMANCE_NLSTEXT |
| 6 | HKEY_PERFORMANCE_TEXT |
| 7 | HKEY_USERS |

### String Functions

```lua
bool string.equal(str1, str2)
```
Compares two strings ignoring case.

### Environment Variables

```lua
int setenv(var, value, override)
string (or nil) getenv(var)
```
`getenv` returns an empty string when a variable is unset.

### Table Functions

```lua
object table.first(table, function(key, value) ... end)
array table.select(table, function(key, value) ... end)
```
- `table.first`: Returns first non-nil result from delegate
- `table.select`: Returns all non-nil values as an array

### Console Window

```lua
ToggleConsole(bool)
SetTitle(newtitle)
```

### DNS & Network

```lua
string or array Dns(name, full default false)
string or nil GetComputerName()
```
- `Dns`: If `full=true`, returns an array of objects with fields `Type` (`"IPV4"`/`"IPV6"`) and `IP`
- If `full=false`, returns first IPv4 address or `nil`
- `GetComputerName`: Retrieve fully qualified computer name

### Memory Status

```lua
int GlobalMemoryStatus(opt type)
```

**Type values:**
| Value | Description |
|-------|-------------|
| 0 | Percentage in use (default) |
| 1 | Total KB of physical memory |
| 2 | Free KB of physical memory |
| 3 | Total KB of paging file |
| 4 | Free KB of paging file |
| 5 | Total KB of virtual memory |
| 6 | Free KB of virtual memory |

### Screen & Cursor

```lua
x, y GetScreenSize()
x, y GetCursorPointPosition()
x, y, monitor GetCursorPosition()
```

### Miscellaneous

```lua
table BencodeDecode(binarystring)
bool GetIsAdmin()
bool SetClipboard(data)
string GetClipboard()
bool GetKeyState(key)
```
- `GetKeyState`: [Virtual Key Codes Reference](https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes)

### Global Variables

| Variable | Description |
|----------|-------------|
| `c` | Table with special characters 0-31 (e.g., `c.LF = '\n'`) |
| `ResList` | Key-value table for ERF container extensions |
| `LAST_TEMP_FILE` | Set by `io.tmpfile()` with created filename |
| `ARGS[]` | Array of command-line parameters |

---

## Console

```lua
bool Console.Create()
bool Console.Destroy()
bool Console.Attach(opt processId)
cursorx, cursory, sizex, sizey, maxsizex, maxsizey Console.GetInfo()
nil Console.SetCursorPosition(x, y)
nil Console.Clear()
charactersprinted Console.Print(...)
key Console.ReadKey()
characterswritten Console.Write(data)
nil Console.SetColor(Background, Foreground)
Background, Foreground Console.GetColor()
nil Console.SetVisible(toggle)
nil Console.SetTitle(newtitle)
```

| Function | Description |
|----------|-------------|
| `Create` | Creates a new console if none exists |
| `Destroy` | Deallocates the console |
| `Attach` | Attaches to existing console (parent if no processId) |
| `GetInfo` | Get console window position and sizes |
| `SetCursorPosition` | Move cursor to new location |
| `Clear` | Empty the console |
| `Print` | Print to console (like default print) |
| `ReadKey` | Read key press, returns `nil` if none pressed |
| `Write` | Write data to console |
| `SetColor` | Set background and foreground colors |
| `GetColor` | Get current colors |
| `SetVisible` | Toggle console visibility |
| `SetTitle` | Set console window title |

---

## Mutex

```lua
Mutex Mutex.Open(name)
-- or on failure:
nil, errorCode Mutex.Open(name)
bool Mutex:Lock(opt timeout)
nil Mutex:Unlock()
islocked, name, internalid Mutex:Info()
```

| Function | Description |
|----------|-------------|
| `Open` | Opens/creates a named mutex (returns `nil, errorCode` on failure) |
| `Lock` | Lock mutex (waits infinitely if no timeout). Returns `true` on success/already-held |
| `Unlock` | Unlocks the mutex |
| `Info` | Get mutex information |

---

## Macro

```lua
x, y Macro.ScreenToMousePoint(x, y)
Macro Macro.Create(inputs)
int Macro:Send()
inputs Macro:GetInputs()
```

### Input Types

**Type 0 - Mouse:**
- `ExtraInfo`, `Flags`, `Time`, `Data`, `X` (0-65536), `Y` (0-65536)
- [Mouse Event Flags](https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mouse_event)

**Type 1 - Keyboard:**
- `ExtraInfo`, `Flags` (0=down, 2=up), `Time`, `Scan`, `Key`
- [Virtual Key Codes](https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes)

---

## Redis

### Connection

```lua
Redis Redis.Open(host, port, opt useTls, opt timeout, opt sslOptions)
```

**SSL Options:**
- `cacert`, `capath`, `cert`, `privatekey`, `servername`
- `verifymode`: 0=none, 1=peer, 2=fail if no peer cert, 4=once, 8=handshake

### Commands

```lua
reply Redis:Command(command, arg, arg, arg, ...)
channel, message Redis:Poll()
```

**Reply types:**
| Value | Type |
|-------|------|
| 1 | REDIS_REPLY_STRING |
| 2 | REDIS_REPLY_ARRAY |
| 3 | REDIS_REPLY_INTEGER |
| 4 | REDIS_REPLY_NIL |
| 5 | REDIS_REPLY_STATUS |
| 6 | REDIS_REPLY_ERROR |
| 7 | REDIS_REPLY_DOUBLE |
| 8 | REDIS_REPLY_BOOL |
| 9 | REDIS_REPLY_MAP |
| 10 | REDIS_REPLY_SET |
| 11 | REDIS_REPLY_ATTR |
| 12 | REDIS_REPLY_PUSH |
| 13 | REDIS_REPLY_BIGNUM |
| 14 | REDIS_REPLY_VERB |

### Data Types

```lua
RedisString Redis:GetString(key)
RedisValue Redis:GetHashset(key)
RedisValue Redis:GetList(key)
RedisValue Redis:GetSet(key)
RedisValue Redis:GetSortedSet(key)
RedisStream Redis:GetStream(key)
```

### RedisStream

```lua
id RedisStream:Add({key=value})
id, data RedisStream:Read(opt id, opt blocktime)
int RedisStream:Trim(int or id)
```

### RedisKey

```lua
bool RedisKey:Delete()
string RedisKey:Type()
int RedisKey:GetTTL()
bool RedisKey:SetTTL(ms)
```

### RedisString

```lua
int RedisString:GetTTL()
bool RedisString:SetTTL(ms)
string RedisString:Set(newValue)
string RedisString:GetOrSet(newValue)
string RedisString:Delete()
length RedisString:len()
```

### Iterator Example

```lua
for key in redis do
    print(key)
end
```

---

## CSV

```lua
csv CSV.Create()
table csv:DecodeString(string)
table csv:DecodeFile(fileName)
```

Returns table with `Comments` (array of strings) and `Rows` (array of rows). Strings are Wchars.

---

## Kafka

### Consumer/Producer

```lua
Kafka Kafka.NewConsumer(opt conf)
Kafka Kafka.NewProducer(opt conf)
```

Default group IDs: `LUAC` (consumer), `LUAP` (producer)

### Operations

```lua
bool, errormsg Kafka:Send(topic, data, opt partition, opt key, opt timeout, opt headers)
event Kafka:Events()
bool Kafka:AddBroker(address)
Groups Kafka:GetGroups(opt group, opt timeout)
Metadata Kafka:GetMetadata(opt timeout)
string Kafka.Logs(opt filename)
bool, errormsg Kafka:Subscribe(topic, opt partition)
bool, errormsg Kafka:Assign(topic, partition)
kafkamessage Kafka:Poll()
kafkamessage Kafka:Consume(topicobject)
int Kafka:GetCommitted(topic, partition)
bool, err Kafka:Seek(topic, partition, offset, opt timeout)
bool, low, high Kafka:GetOffsets(topic, partition, opt timeout)
```

### Topic Management

```lua
kafkatopic, errormsg Kafka:OpenTopic(topicname, opt partition, offset, opt conf)
table, errormsg Kafka:CreateTopic(topicname, opt partitions, opt replicationfactor, opt brokerid, opt requesttimeout, opt operationtimeout)
table, errormsg Kafka:DeleteTopic(topicname)
table, errormsg Kafka:AlterConfig(resourcetype, resourcename, configname, configvalue, opt timeout)
table Kafka:GetConfig(resourcetype, resourcename, opt timeout)
table, errormsg Kafka:SetPartitions(topicname, newpartitioncount, opt timeout)
bool, errormsg Kafka:PauseTopic(Kafkatopic)
bool, errormsg Kafka:ResumeTopic(Kafkatopic)
int Kafka:GetId()
```

**Resource types:** 1=any, 2=topic, 3=group, 4=broker

### Kafkatopic

```lua
int Kafkatopic:GetOwnerId()
name, partition Kafkatopic:GetInfo()
nil Kafkatopic:Dispose()
bool Kafkatopic:IsPaused()
```

### Kafkamessage

```lua
table Kafkamessage:GetData()
int, int Kafkamessage:GetTimestamp()
int Kafkamessage:GetLatency()
int Kafkamessage:GetOwnerId()
nil Kafkamessage:Dispose()
```

**GetData returns:** `Error`, `ErrorCode`, `Key`, `Offset`, `Partition`, `Payload`, `Topic`, `Headers`

---

## FileAsync

```lua
FileAsync FileAsync.Open(file, mode, opt buffersize)
bool FileAsync:Busy(opt wait, opt cancel)
int FileAsync:Tell()
int FileAsync:Seek(pos, opt type)
nil FileAsync:Rewind()
bool FileAsync:EndOfFile()
nil FileAsync:Read(opt bytestoread, opt readbuffersize)
currentlength, maxlength FileAsync:BufferStatus()
string, bool FileAsync:EmptyBuffer()
nil FileAsync:Close()
```

**Seek types:** 0=start, 1=current, 2=end

---

## FTP

### Connection

```lua
ftp, welcomemessages FTP.Open(address, opt port)
nil FTP:SetEndline(endline)
bool, error FTP:Login(user, password)
bool, error FTP:Command(command)
ip, port FTP:Passive()
nil FTP.SetTimeout(timeinseconds)
msgs FTP:GetMessages(opt timeout)
nil FTP:Close()
isconnected, wsaerror FTP:GetConnectionStatus()
```

### Data Channel

```lua
ftpchannel FTP.OpenDataChannel(address, port)
bool ftpchannel:Send(data)
isalive, data ftpchannel:Recv(opt buffersize)
isalive, hasdata ftpchannel:GetConnectionStatus()
nil ftpchannel:Close()
```

---

## Sound

```lua
bool Sound.Play(wavefile, opt async)
bool Sound.Beep(freq, duration)
int, string Sound.SendMCS(command)
```

---

## ODBC

### Connection

```lua
array ODBC.GetAllDrivers()
ODBC ODBC.DriverConnect(connectionstring)
```

### Query Operations

```lua
bool, error ODBC:Prepare(sql)
bool, error ODBC:Bind(data, opt asbinary)
bool, error ODBC:Execute()
bool, error ODBC:Fetch()
table, error ODBC:GetRow()
table ODBC:GetRowColumnTypes()
```

### Transaction Control

```lua
bool, error ODBC:ToggleAutoCommit(bool)
bool, error ODBC:Begin()
bool, error ODBC:Commit()
bool, error ODBC:Rollback()
```

### Schema Queries

```lua
bool, error ODBC:Tables(schema)
bool, error ODBC:Columns(table, schema)
bool, error ODBC:SpecialColumns(table, schema)
bool, error ODBC:PrimaryKeys(table, schema)
bool, error ODBC:ForeignKeys(table, schema)
bool, error ODBC:Procedures(schema)
bool, error ODBC:ProcedureColumns(procedure, schema)
nil ODBC:Disconnect()
```

---

## Archive

```lua
Archive Archive.OpenRead(filename, opt usewchar)
array Archive:Entries()
file, size Archive:SetEntry(index)
data Archive:Read(opt buffer)
```

**Entries returns:** Array of tables with `Name` and `Size`

---

## Stream

### Creation

```lua
Stream Stream.Create(opt initialsize/allocfunc)
Stream Stream.FromString(string)
Stream Stream.Open(filename)
table Stream.GetSharedMemoryStreamInfo(name)
Stream Stream.CreateSharedMemoryStream(name, size)
Stream Stream.OpenSharedMemoryStream(name, opt readonly)
```

### File Operations

```lua
nil Stream:Save(filename)
nil Stream:WriteToFile(filename, pos, len)
nil Stream:ReadFromFile(filename, pos, len)
```

### Read/Write Operations

```lua
bool, err Stream:WriteByte(byte)
byte Stream:ReadByte()
byte Stream:PeekByte(opt pos)
void Stream:SetByte(byte, position)
int Stream:Write(obj, opt size)
string, int Stream:ReadUtf8()
int Stream:Buffer(str)
void Stream:Shrink()
string Stream:Read(opt length)
string Stream:ReadUntil(opt tofind)
pos Stream:IndexOf(string or byte)
```

### Stream Info

```lua
position, length, allocated Stream:GetInfo()
void Stream:SetLength(newlength)
length Stream:len()
pos Stream:pos()
void Stream:Seek(opt pos)
```

### Compression

```lua
Stream Stream:Compress(opt algorithm)
Stream Stream:Decompress(opt algorithm)
```

**Algorithms:**
| Value | Algorithm |
|-------|-----------|
| 0 | COMPRESS_ALGORITHM_INVALID |
| 1 | COMPRESS_ALGORITHM_NULL |
| 2 | COMPRESS_ALGORITHM_MSZIP (default) |
| 3 | COMPRESS_ALGORITHM_XPRESS |
| 4 | COMPRESS_ALGORITHM_XPRESS_HUFF |
| 5 | COMPRESS_ALGORITHM_LZMS |
| 6 | COMPRESS_ALGORITHM_MAX |

### Typed Read/Write

```lua
bool Stream:WriteFloat() / number Stream:ReadFloat()
bool Stream:WriteDouble() / number Stream:ReadDouble()
bool Stream:WriteShort() / int Stream:ReadShort()
bool Stream:WriteUnsignedShort() / int Stream:ReadUnsignedShort()
bool Stream:WriteInt() / int Stream:ReadInt()
bool Stream:WriteUnsignedInt() / int Stream:ReadUnsignedInt()
bool Stream:WriteLong() / int Stream:ReadLong()
bool Stream:WriteUnsignedLong() / int Stream:ReadUnsignedLong()
```

---

## TTS

```lua
TTS TTS.Create()
array TTS.GetVoices()
bool TTS:Speak(text, opt flags)
bool TTS:GetIsSpeaking()
nil TTS:PlayPause()
bool TTS:GetIsPaused()
int TTS:GetVolume()
bool TTS:SetVolume(volume)
int TTS:GetRate()
bool TTS:SetRate(rate)
bool TTS:SetVoice(voicename)
bool TTS:Skip()
nil TTS:Dispose()
```

---

## Env

```lua
table Env.Create(name)
table Env.Get(name)
table Env.GetOrCreate(name)
table Env.Meta()
```

---

## Zip

```lua
Zip Zip.Open(zipfile)
int Zip:AddFile(key, file)
int Zip:AddData(key, data)
true/data Zip:Extract(key/index, opt targetfile)
table Zip:GetInfo(key/index)
int Zip:Delete(key/index)
array Zip:GetFiles()
nil Zip:Close()
```

**GetInfo returns:** `comp_method`, `comp_size`, `crc`, `encryption_method`, `flags`, `index`, `mtime`, `name`, `size`, `valid`

---

## Server

```lua
Server Server.Start(port)
nil Server:Stop()
event/nil Server:GetEvent()
bool Server:Disconnect(socket)
bool Server:Send(socket, data)
nil Server.SetStartFunc(function)
table Server:GetClients()
```

**Event types:** `socket`, `type`, `data`

---

## Client

```lua
Client Client.Connect(address, port)
bool/error Client:Status()
nil Client:Disconnect()
event/nil Client:GetEvent()
bool Client:Send(data)
```

**Network event types:**
| Value | Type |
|-------|------|
| 1 | NETEVENT_CONNECTED |
| 2 | NETEVENT_DISCONNECTED |
| 3 | NETEVENT_SEND |
| 4 | NETEVENT_RECEIVE |

---

## Pipe

```lua
Pipe Pipe.Create(name, opt maxinstances, opt buffersize, opt timeout, opt write, opt read)
Pipe Pipe.Open(name, opt write, opt read)
int Pipe:Write(string)
string Pipe:Read(opt buffersize)
int Pipe:ReadByte()
bool Pipe:WriteByte(byte)
avail Pipe:Available()
nil Pipe:Close()
```

---

## Base64

```lua
base64string Base64.Encode(string)
string Base64.Decode(base64string)
string Base64.GetEncodeTable()
void Base64.SetEncodeTable(encodetablestring)
```

---

## Socket

### Connection

```lua
Socket Socket.Connect(address, port, family, sockettype, protocol)
Socket Socket.Listen(port, family, sockettype, protocol)
```

**Parameters:**
- `family`: 2=IPv4, 23=IPv6
- `sockettype`: 1=stream, 2=datagram, 3=raw, 4=rdm, 5=seqpacket
- `protocol`: 6=TCP, 17=UDP

### Operations

```lua
bool, bool, bool, bool Socket:Select()
bool/nil Socket:HasData()
string Socket:Read()
int Socket:Write(data)
Socket Socket:Accept()
address, port, socketid Socket:Info()
nil Socket:Close()
```

---

## Services

```lua
array Services.All()
Service Services.Open(servicename, opt readonly)
status Service:Status()
config Service:Config()
bool Service:Start()
bool Service:Stop()
```

---

## Aes

```lua
Aes Aes.Create(key, opt iv, opt usectr)
data Aes:Encrypt(data)
data Aes:Decrypt(data)
nil Aes:SetIV(opt iv)
```

Creates AES-256-CBC, AES-256-ECB, or AES-256-CTR context.

---

## Process

```lua
table Process.All()
Process Process.Open(opt id)
Process Process.Start(app, cmd, directory, noconsole, opt redirectinputoutput)
string Process:ReadFromPipe(opt buffersize)
int Process:WriteToPipe()
string Process:ReadErrorFromPipe(opt buffersize)
bool Process:Stop()
int/nil Process:GetExitCode()
int Process:GetID()
string Process:GetName()
number Process:GetCPU()
number Process:GetRAM()
int/bool Process:Priority(opt prio)
int, int Process:Affinity(opt newmask)
array Process:Threads()
```

---

## Imgui

### Creation

```lua
Imgui Imgui.Create(title, tag, width, height, renderfunction)
float Imgui.GetFontSize()
nil Imgui.SetStyle(table)
table Imgui.GetStyle()
```

### Value Types

| Type | Description |
|------|-------------|
| 1 | bool |
| 2 | float |
| 3 | vec4 {x,y,z,w} |
| 4 | int |
| 5 | string |
| 6 | double |

### Operations

```lua
value Imgui:GetValue(tag, opt type)
nil Imgui:SetValue(tag, type, value)
array Imgui:GetAllValues()
bool Imgui:Tick()
nil Imgui:Close()
nil Imgui:Quit(opt code)
info Imgui:Info()
nil Imgui:Clear()
```

### Helpers

```lua
table Imgui.GetEnums()
nil Imgui.SetClipboardText(text)
string Imgui.GetClipboardText()
r, g, b Imgui.Vec4ToRGB(vector4)
vec4 Imgui.RGBToVec4(r, g, b)
```

### ImguiDraw Functions

**Layout:**
```lua
nil ImguiDraw:SameLine(opt offsetstartx, opt spacing)
vec2 ImguiDraw:GetCursorPos()
vec2 ImguiDraw:GetCursorStartPos()
nil ImguiDraw:Indent(opt w)
nil ImguiDraw:Unindent(opt w)
nil ImguiDraw:BeginGroup() / nil ImguiDraw:EndGroup()
nil ImguiDraw:PushStyleVar(flag, value)
nil ImguiDraw:PopStyleVar(opt count)
vec2 ImguiDraw:GetWindowSize()
nil ImguiDraw:SetNextItemWidth(width)
```

**Text:**
```lua
nil ImguiDraw:Text(text)
nil ImguiDraw:TextWrapped(text)
nil ImguiDraw:TextColored(color, text)
vec2 ImguiDraw:CalcTextSize(string)
```

**Input:**
```lua
bool ImguiDraw:InputText(label, tag, opt hint)
bool ImguiDraw:InputTextMultiline(label, tag, size, flags)
bool ImguiDraw:InputInt(label, tag, opt step, opt faststep, opt flags)
bool ImguiDraw:InputFloat(label, tag, opt step, opt faststep, opt format, opt flags)
bool ImguiDraw:InputDouble(label, tag, opt step, opt faststep, opt format, opt flags)
```

**Controls:**
```lua
bool ImguiDraw:Button(title)
bool ImguiDraw:Checkbox(title, tag)
bool ImguiDraw:RadioButton(title, tag, id)
bool ImguiDraw:Combo(label, tag, stringarray, opt maxitemsshown)
bool ImguiDraw:SliderFloat(title, tag, min, max, opt format, opt flags)
bool ImguiDraw:SliderInt(title, tag, min, max, opt format, opt flags)
bool ImguiDraw:ColorEdit3(title, tag)
bool ImguiDraw:Selectable(text, selected)
```

**Windows/Menus:**
```lua
bool ImguiDraw:Begin(title, opt tag, flags)
nil ImguiDraw:End()
bool ImguiDraw:BeginChild(title, width, height, noborder)
nil ImguiDraw:EndChild()
bool ImguiDraw:BeginMainMenuBar() / nil ImguiDraw:EndMainMenuBar()
bool ImguiDraw:BeginMenu(title, disabled) / nil ImguiDraw:EndMenu()
bool ImguiDraw:MenuItem(title)
bool ImguiDraw:BeginTabBar(id, flags) / nil ImguiDraw:EndTabBar()
bool ImguiDraw:BeginTabItem(label, opt tag, flags) / nil ImguiDraw:EndTabItem()
```

**Tables:**
```lua
bool ImguiDraw:BeginTable(labelid, columns, opt flags)
nil ImguiDraw:EndTable()
nil ImguiDraw:TableSetupColumn(columnname, flags, widthorweight)
bool ImguiDraw:TableNextColumn()
bool ImguiDraw:TableSetColumnIndex(idx)
nil ImguiDraw:TableNextRow(opt flags, opt row_min_height)
```

**Utilities:**
```lua
nil ImguiDraw:Separator()
nil ImguiDraw:ShowDemoWindow(opt tag)
bool ImguiDraw:IsItemHovered(opt flags)
-- Current implementation does not return a Lua boolean:
nil ImguiDraw:IsItemClicked(opt mousebutton)
nil ImguiDraw:IsMouseDoubleClicked(opt mousebutton)
```

---

## HTTP

```lua
Http Http.Start(method, url, content, headers, opt usehttp1.0)
nil Http:SetTimeout(timeout)
code, ok, contents, header Http:GetResult()
IsRunning, status, runtime, recv, send Http:GetStatus()
file Http:GetRaw()
bool Http:Wait(opt timeout)
```

---

## Hashing

### SHA256

```lua
SHA256 SHA256.New()
nil SHA256:Update(data)
hexstring, 32bytes SHA256:Finish()
```

### MD5

```lua
MD5 MD5.New()
nil MD5:Update(data)
hexstring, 16bytes MD5:Finish()
```

### SHA1

```lua
SHA1 SHA1.New()
nil SHA1:Update(data)
hexstring, 20bytes SHA1:Finish()
```

---

## MySQL

```lua
MySQL MySQL.Connect(address, user, password, database, opt port, opt timeout)
string MySQL.EscapeValue(value)
bool, txt MySQL:Query(query)
bool MySQL:IsBusy()
array MySQL:GetResultRow()
array MySQL:GetResultFields()
array MySQL:GetResult()
MySQL:Close()
```

---

## Timer

```lua
Timer Timer.New()
bool Timer:IsRunning()
nil Timer:Reset()
nil Timer:Start()
nil Timer:Stop()
number Timer:Elapsed()
```

---

## SQLite

```lua
SQLite SQLite.Open(opt filename, opt mode)
bool, txt SQLite:Query(querystring, preparedstatements)
nil SQLite:Finish()
bool SQLite:Fetch()
table SQLite:GetRow(opt index)
nil SQLite:RegisterFunction(function, name, args)
nil SQLite:RegisterAggregateFunction(function, name, args)
nil SQLite:ToggleWidechar(bool)
nil SQLite:Close()
```

**Mode:** 0=single thread, 1=multithreaded, 2=serialized

---

## Image

*All coordinates are 1-indexed*

```lua
Image Image.Screenshot(width, height, startx, starty, monitor)
bool Image:Save(filename)
Image Image.Load(filename)
Image Image.Create(Width, Height)
Image Image:Crop(Width, Height, StartX, StartY)
array Image:GetPixels()
bool Image:SetPixels(pixel)
matrix Image:GetPixelMatrix()
bool Image:SetPixelMatrix(pixelmatrix)
pixel Image:GetPixel(y, x)
void Image:SetPixel(y, x, pixel)
width, height Image:GetSize()
void Image:Close()
```

---

## Json

```lua
Json Json.Create(opt pretty)
nil/value Json:SetNullValue(value)
jsonstring Json:Encode(table)
table Json:Decode(jsonstring)
void Json:EncodeToFile(filename, table)
table Json:DecodeFromFile(filename)
void Json:EncodeToFunction(func, table)
table Json:DecodeFromFunction(func)
void Json:Dispose()
coroutine Json:Iterator(function)
```

### Coroutine Example

```lua
jsonstring = Json:Encode(coroutine.create(function()
    coroutine.yield(nil, {})  -- Root element
    coroutine.yield("MyTable", {})  -- Object
    for n = 1, 10 do
        coroutine.yield(n, "Hello")
    end
    coroutine.yield(nil, nil)  -- Step out
    coroutine.yield("Cake", "Is good")
    coroutine.yield(nil, nil)  -- Finish
end))
-- Result: {"MyTable":["Hello",...], "Cake":"Is good"}
```

---

## Wchar

### Creation

```lua
Wchar Wchar.FromAnsi(str)
Wchar Wchar.FromBytes(array or widestring or int)
Wchar Wchar.FromUtf8(str)
nil Wchar.Setlocale(codepage)
```

### Conversion

```lua
string Wchar:ToUtf8()
string Wchar:ToAnsi()
string Wchar:ToWide()
array Wchar:ToBytes()
```

### Operations

```lua
int Wchar:At(index)
array Wchar:Codepoints()
Wchar Wchar:ToLower()
Wchar Wchar:ToUpper()
Wchar Wchar:Substring(start, opt length)
int Wchar:Find(substring, opt offset)
```

### Metamethods

- `tostring`: same as `ToUtf8`
- `..` (concat): returns new Wchar
- `#` (length): returns length
- `==` (equal): compares Wchars

---

## FileSystem

### File/Directory Operations

```lua
Array FileSystem.GetFiles(path, opt filter)
Array FileSystem.GetDirectories(path, opt filter)
FileInfo FileSystem.GetFileInfo(file)
FileInfo FileSystem.GetFileInfoWide(file)
Array FileSystem.GetAll(path)
Array FileSystem.GetAllWide(widepath)
file FileSystem.OpenFileWide(widefilename, widemode)
bool FileSystem.RenameWide(src, dst)
bool FileSystem.SetAttributes(filename, attributemask)
bool FileSystem.Copy(source, destination, overwrite)
bool FileSystem.Move(source, destination)
bool FileSystem.Delete(source)
bool FileSystem.CreateDirectory(path)
bool FileSystem.RemoveDirectory(path)
bool FileSystem.Rename(source, destination)
```

### Paths

```lua
WChar FileSystem.GetSpecialFolder(csidl)
string FileSystem.CurrentDirectory()
WChar FileSystem.CurrentDirectoryWide()
bool FileSystem.SetCurrentDirectory(dir)
string FileSystem.GetTempFileName(opt pathonly)
array FileSystem.GetDrives(opt drive)
```

### CSIDL Constants

| Value | Folder |
|-------|--------|
| 0x0000 | Desktop |
| 0x0005 | My Documents |
| 0x000d | My Music |
| 0x000e | My Videos |
| 0x0010 | Desktop Directory |
| 0x001a | AppData |

---

## TWODA

```lua
TWODA TWODA.Open(file)
string TWODA:Get2DAString(row, index or columnname)
table TWODA:Get2DARow(row)
array, numrows, version TWODA:GetInfo()
```

---

## TLK

```lua
TLK TLK.Create(filename, array, opt languageid, opt version)
TLK TLK.Open(filename)
table TLK:GetAll()
table TLK:Get(strref)
bool TLK:SetSoundInfo(strref, soundresref, opt soundlength)
bool TLK:Set(strref, newstring)
bool TLK:Defragment(opt extra)
count, languageid, version TLK:GetInfo()
```

---

## KeyBif

```lua
KeyBif KeyBif.Create()
```

---

## ERF

```lua
ERF, entries, filetype, version ERF.Open(filename)
ERF ERF.Create(filename, typeheader, filelist, opt version, opt desc)
header ERF:GetHeader()
array ERF:GetStrings()
array ERF:GetKeys()
binary ERF:GetResource(ResID)
nil ERF:Extract(ResID, targetfile)
```

---

## GFF

```lua
struct GFF.OpenFile(file)
struct GFF.OpenString(string)
nil GFF.SaveToFile(gff, file)
string GFF.SaveToString(gff)
```

### GFF Types

| Value | Type |
|-------|------|
| 0 | byte |
| 1 | char |
| 2 | word |
| 3 | short |
| 4 | dword |
| 5 | int |
| 6 | dword64 |
| 7 | int64 |
| 8 | float |
| 9 | double |
| 10 | CExoString |
| 11 | ResRef |
| 12 | CExoLocString |
| 13 | binary string |
| 14 | struct |
| 15 | list |

