#pragma once

const auto script = R"!(
var socketAPs;
if(!socketAPs || socketAPs.readyState !== WebSocket.OPEN)
{
    socketAPs = new WebSocket("ws://4.3.2.1/ws");
} 
socketAPs.addEventListener('message',handleApListMessage);
socketAPs.addEventListener('open', function (event) {
refreshAps();});
var socketCustomParams;
if(!socketCustomParams || socketCustomParams.readyState !== WebSocket.OPEN)
{
    socketCustomParams = new WebSocket("ws://4.3.2.1/cp");
} 
socketCustomParams.addEventListener('message',handleCustomParamsMessage);
socketCustomParams.addEventListener('open', function (event) {
    refreshCustomParams();});
var socketLogger;
if(!socketLogger || socketLogger.readyState !== WebSocket.OPEN)
{
    socketLogger = new WebSocket("ws://4.3.2.1/log");
} 
socketLogger.addEventListener('message',handleLogs);
socketLogger.addEventListener('open', function (event) {
      socketLogger.send("Logger opened");});
function sendCredentials() {
    var ssid = document.getElementById("ssid-input").value;
    var password = document.getElementById("password-input").value;
    var url = "http://4.3.2.1/postCredentials";

    var xhr = new XMLHttpRequest();
    xhr.open("POST", url, true);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.setRequestHeader('Access-Control-Allow-Headers', '*');
    xhr.setRequestHeader('Access-Control-Allow-Origin', '*');
    var data = JSON.stringify({ "ssid": ssid, "password": password });
    xhr.send(data);
}

function handleTileClick(tile) {
    var tiles = document.querySelectorAll('.tile');

    tiles.forEach(function (tile) {
        tile.classList.remove('active');
    });

    tile.classList.add('active');
    document.getElementById("ssid-input").value=tile.innerHTML;
}

function refreshAps()
{
    socketAPs.send("ss");
    apsList = document.getElementById("APs");
    apsList.innerHTML= '';
}

function refreshCustomParams()
{
    socketCustomParams.send("ping");

}

function handleApListMessage(event)
{
    console.log(event.data);
    apsList = document.getElementById("APs");
    jsonObject = JSON.parse(event.data);
    for (var key in jsonObject) {
    if (jsonObject.hasOwnProperty(key)) {
        var newDiv = document.createElement('div');
        newDiv.className = 'tile';
        newDiv.textContent = key;
        newDiv.onclick = function() {handleTileClick(this);};
        apsList.appendChild(newDiv);
    }
    }
}
function handleCustomParamsMessage(event)
{
console.log(event.data);
var htmlElemet = document.createElement("h2");
htmlElemet.textContent = "Custom parameters";
var cpDiv = document.getElementById("CP");
cpDiv.appendChild(htmlElemet);
jsonObject = JSON.parse(event.data);
for (var key in jsonObject) {
if (jsonObject.hasOwnProperty(key)) {
    var label = document.createElement("label");
    label.textContent = key + ": ";
    htmlElemet = document.createElement("input");
    htmlElemet.type = "text";
    htmlElemet.name = key;
    htmlElemet.value = jsonObject[key];
    var newDiv = document.createElement('div');
    newDiv.appendChild(label);
    newDiv.appendChild(htmlElemet);
    cpDiv.appendChild(newDiv);
    }
}  
htmlElemet = document.createElement("button");
htmlElemet.textContent = "Send";
htmlElemet.onclick = function(){sendBackCP();};
cpDiv.appendChild(htmlElemet);
}
function sendBackCP()
{
    var updatedObject = {};
    var inputElements = document.querySelectorAll("#CP input");
    inputElements.forEach(function (inputElement) {
        updatedObject[inputElement.name] = inputElement.value;
    });
    console.log("Updated Object:", updatedObject);
    var jsonString = JSON.stringify(updatedObject);
    socketCustomParams.send(jsonString);
}
function handleLogs(event)
{
    if (!document.getElementById('LOG')) {
      var textarea = document.createElement('textarea');
      textarea.id = 'LOG';
      textarea.readOnly = true;
      document.body.appendChild(textarea);
      }
      var textarea = document.getElementById('LOG');
      textarea.value += event.data + '\n';
      textarea.scrollTop = textarea.scrollHeight;
}
)!";