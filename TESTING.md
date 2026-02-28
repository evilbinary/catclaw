# Testing CatClaw

## Prerequisites

- C compiler (gcc, MSVC, etc.)
- libcurl library (for AI model API integration)
- pthread library (for threading support)
- Winsock library (for Windows systems)

## Compilation

### Using gcc

```bash
gcc -Wall -Wextra -O2 src/*.c -o catclaw -lcurl -lpthread -lws2_32
```

### Using MSVC

```bash
cl /W4 /O2 src\*.c /link /out:catclaw.exe /libpath:"C:\path\to\curl\lib" libcurl.lib ws2_32.lib
```

## Configuration

Create a configuration file at `~/.catclaw/config.json`:

```json
{
  "model": "anthropic/claude-3-opus-20240229",
  "gateway_port": 18789,
  "workspace_dir": "~/.catclaw/workspace",
  "browser_enabled": false
}
```

## Environment Variables

Set the API key for your chosen AI model:

### For Anthropic

```bash
export ANTHROPIC_API_KEY="your-api-key"
```

### For OpenAI

```bash
export OPENAI_API_KEY="your-api-key"
```

## Running

```bash
./catclaw
```

## Testing Commands

1. **help** - Show available commands
2. **status** - Show system status
3. **message <text>** - Send a message to the AI
4. **gateway start** - Start the gateway server
5. **gateway stop** - Stop the gateway server
6. **gateway status** - Show gateway status
7. **exit** - Exit the program

## Testing WebSocket

Use a WebSocket client (like [Postman](https://www.postman.com/) or [wscat](https://github.com/websockets/wscat)) to connect to `ws://localhost:18789`.

### Example using wscat

```bash
wscat -c ws://localhost:18789
> Hello, CatClaw!
< Hello, CatClaw!
< [AI response]
```

## Testing AI Model Integration

1. Set the appropriate API key environment variable
2. Run CatClaw
3. Send a message using the `message` command
4. Check the response from the AI model

## Testing Channels

### WebChat

1. Connect to the WebSocket server
2. Send a message
3. Check that the message is processed by the AI and a response is returned

## Troubleshooting

### Common Issues

1. **WebSocket connection failed** - Check that the gateway server is running and the port is correct
2. **AI model error** - Check that the API key is set correctly and the model name is valid
3. **Compilation errors** - Ensure all required libraries are installed and linked correctly

### Logs

Check the console output for error messages and debugging information.
