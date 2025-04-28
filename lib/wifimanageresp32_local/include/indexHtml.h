#pragma once

const auto html_page = R"!(
<!DOCTYPE html>
<html>
<head>
  <title>Universal WiFi Manager</title>
   <link rel="stylesheet" type="text/css" href="styles.css">
</head>
<body>
  <h1>Universal WiFi Manager</h1>
  <h2>Available APs</h2>
  <div id="APs">None</div>
    <button type="button" onclick="refreshAps()">Refresh</button>

  <form id="connection-form">
    <input type="text" id="ssid-input" placeholder="SSID" required><br>
    <input type="password" id="password-input" placeholder="Password" required><br>
    <button type="button" onclick="sendCredentials()">Connect</button>
  </form>
  <div id="CP"></div>
  <script src="myScript.js"></script>
</body>
</html>
)!";