<?php
// Enable full error reporting for debugging
error_reporting(E_ALL);
ini_set('display_errors', '1');

date_default_timezone_set('Asia/Kolkata');

// Log file path
$logFile = __DIR__ . '/error_log.txt';

/**
 * Append a debug message with timestamp to the log file.
 *
 * @param string $msg The message to log.
 */
function logMsg($msg) {
    global $logFile;
    $time = date('Y-m-d H:i:s');
    file_put_contents($logFile, "[$time] $msg" . PHP_EOL, FILE_APPEND);
    echo "[$time] $msg" . PHP_EOL;
}

// Server configuration
$host = '0.0.0.0';
$port = 3000;

// SSL context options
//$context = stream_context_create([
   // 'ssl' => [
       // 'local_cert'        => 'cert.pem',    // Path to your SSL certificate
        //'local_pk'          => 'privkey.pem', // Path to your private key
  $certPath = '/etc/letsencrypt/live/socket.jpfashion.in/cert.pem';
$keyPath  = '/etc/letsencrypt/live/socket.jpfashion.in/privkey.pem';

$context = stream_context_create([
    'ssl' => [
        'local_cert'        => $certPath,
        'local_pk'          => $keyPath,      

'verify_peer'       => false,         // Disable peer verification for testing
        'verify_peer_name'  => false,
        'allow_self_signed' => true,          // Allow self-signed certificates for testing
    ]
]);

// Create the server socket using ssl:// protocol
//$socket = stream_socket_server("ssl://$host:$port", $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $context);
$socket = stream_socket_server("tls://$host:$port", $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $context);

if (!$socket) {
    logMsg("Error creating socket: $errstr ($errno)");
    die("Error creating socket: $errstr ($errno)\n");
}

logMsg("WebSocket server listening on $host:$port");

$clients = [];
$handshakes = [];

// Time tracking for ping message (send every 15 minutes)
$lastPingTime = time();
$pingInterval = 15 * 60;  // 15 minutes in seconds

while (true) {
    // Clean up disconnected clients
    $validClients = [];
    foreach ($clients as $client) {
        if (is_resource($client)) {
            $meta = stream_get_meta_data($client);
            if (!$meta['eof']) {
                $validClients[] = $client;
            } else {
                logMsg("Client detected as disconnected (EOF).");
                fclose($client);
                unset($handshakes[(int)$client]);
            }
        }
    }
    $clients = $validClients;

    // Ensure main socket is valid
    if ($socket === false) {
        logMsg("Socket is invalid. Exiting.");
        die("Socket is invalid.\n");
    }

    // Prepare arrays for stream_select
    $read = array_merge([$socket], $clients);
    $write = $except = [];

    if (stream_select($read, $write, $except, 0, 200000) < 1) { // 200ms timeout
        continue;
    }

    // New connection
    if (in_array($socket, $read)) {
  //      $newClient = @stream_socket_accept($socket, 0, $peerName);
$newClient = @stream_socket_accept($socket, 0.5, $peerName);
    
    if ($newClient === false) {
            logMsg("Failed to accept new client connection.");
        } else {
            stream_set_blocking($newClient, false);
            $clients[] = $newClient;
            $handshakes[(int)$newClient] = false;
            logMsg("New client connected from $peerName.");
        }
        // Remove main socket from read array
        unset($read[array_search($socket, $read)]);
    }

    // Process data from clients
    foreach ($read as $client) {
        $data = @fread($client, 1024);
        if ($data === false || $data === '') {
            $meta = stream_get_meta_data($client);
            if ($meta['timed_out'] || $meta['eof']) {
                $index = array_search($client, $clients);
                fclose($client);
                unset($clients[$index], $handshakes[(int)$client]);
                logMsg("Client disconnected (read error or EOF).");
            }
            continue;
        }

        // If handshake not completed, process it
        if (!$handshakes[(int)$client]) {
            $headers = [];
            $lines = preg_split("/\r\n/", $data);
            foreach ($lines as $line) {
                if (preg_match('/\A(\S+): (.*)\z/', $line, $matches)) {
                    $headers[$matches[1]] = $matches[2];
                }
            }

            if (isset($headers['Sec-WebSocket-Key'])) {
                $key = $headers['Sec-WebSocket-Key'];
                $acceptKey = base64_encode(sha1($key . '258EAFA5-E914-47DA-95CA-C5AB0DC85B11', true));
                $response = "HTTP/1.1 101 Switching Protocols\r\n" .
                            "Upgrade: websocket\r\n" .
                            "Connection: Upgrade\r\n" .
                            "Sec-WebSocket-Accept: $acceptKey\r\n\r\n";
                fwrite($client, $response);
                $handshakes[(int)$client] = true;
                logMsg("Handshake completed with client.");
                continue;
            } else {
                logMsg("Invalid handshake request received.");
                fclose($client);
                continue;
            }
        }

        // Decode WebSocket frame
        $decoded = decodeWebsocketFrame($data);
        if ($decoded['opcode'] === 8) { // Connection close opcode
            fclose($client);
            logMsg("Client sent close opcode. Connection closed.");
            continue;
        }

        $message = $decoded['payload'];
        logMsg("Received message: $message");

        // Relay control: broadcast message to all other clients
        foreach ($clients as $receiver) {
            if ($receiver !== $client && is_resource($receiver)) {
                fwrite($receiver, encodeWebsocketFrame($message));
            }
        }
    }

    // Send ping message to all clients every 15 minutes
    if ((time() - $lastPingTime) >= $pingInterval) {
        foreach ($clients as $client) {
            if (is_resource($client)) {
                // Send a simple "ping" message
                fwrite($client, encodeWebsocketFrame('ping'));
                logMsg("Sent ping message to client.");
            }
        }
        $lastPingTime = time();  // Reset the last ping time
    }
}

/**
 * Decodes a WebSocket frame.
 *
 * @param string $data The raw frame data.
 * @return array An associative array with 'opcode' and 'payload'.
 */
function decodeWebsocketFrame($data) {
    $unmaskedPayload = '';
    $decodedData = [];

    $firstByteBinary = sprintf('%08b', ord($data[0]));
    $secondByteBinary = sprintf('%08b', ord($data[1]));

    $opcode = bindec(substr($firstByteBinary, 4, 4));
    $isMasked = ($secondByteBinary[0] == '1');
    $payloadLength = ord($data[1]) & 127;

    $offset = 2;
    if ($payloadLength === 126) {
        $offset += 2;
    } elseif ($payloadLength === 127) {
        $offset += 8;
    }

    if ($isMasked) {
        $maskingKey = substr($data, $offset, 4);
        $offset += 4;
    }

    $payload = substr($data, $offset, $payloadLength);

    if ($isMasked) {
        for ($i = 0; $i < strlen($payload); $i++) {
            $unmaskedPayload .= $payload[$i] ^ $maskingKey[$i % 4];
        }
    } else {
        $unmaskedPayload = $payload;
    }

    return [
        'opcode' => $opcode,
        'payload' => $unmaskedPayload
    ];
}

/**
 * Encodes a payload into a WebSocket frame.
 *
 * @param string $payload The data to send.
 * @param int $opcode The opcode (default is 1 for text).
 * @return string The encoded WebSocket frame.
 */
function encodeWebsocketFrame($payload, $opcode = 1) {
    $frame = [];
    $payloadLength = strlen($payload);

    // First byte: FIN flag set plus opcode
    $frame[0] = 0x80 | $opcode;

    // Second byte: payload length
    if ($payloadLength <= 125) {
        $frame[1] = $payloadLength;
        $offset = 2;
    } elseif ($payloadLength <= 65535) {
        $frame[1] = 126;
        $frame[2] = ($payloadLength >> 8) & 255;
        $frame[3] = $payloadLength & 255;
        $offset = 4;
    } else {
        $frame[1] = 127;
        for ($i = 7; $i >= 0; $i--) {
            $frame[8 - $i] = ($payloadLength >> (8 * $i)) & 255;
        }
        $offset = 10;
    }

    // Convert frame bytes to characters and append payload (server messages are not masked)
    $frameStr = '';
    foreach ($frame as $byte) {
        $frameStr .= chr($byte);
    }
    return $frameStr . $payload;
}
?>
