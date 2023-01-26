/*******************************************************************************
 * Sketch name: AJAX code with JSON parsing
 * Description: Webpage code with JSON for revised Listing 8-1
 * Created on:  October 2020
 * Author:      Neil Cameron
 * Book:        Electronics Projects with the ESP8266 and ESP32
 * Chapter :    8 - Updating a webpage
 ******************************************************************************/

char page_dbg[] PROGMEM = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <!----------------------------CSS---------------------------->
    <style>
        h2 {font-family: arial;text-align: left;color: #2a2671;}
        body {margin: 20px;background-color: LightGray;}
        #varMesT{color:red;font-size:24px;}
        #varCurrT{color: #002aff;font-size:24px;}
        .block1 {width: 200px; background: #ccc; padding: 5px; padding-right: 20px; 
        border: solid 1px black; float: left; }
        .block2 { width: 200px; padding: 5px; border: solid 1px black; float: left; 
        position: relative; left: 10px; }
    </style>
    <!----------------------------HTML--------------------------->
    <title>Heat Table ESP32</title>
</head>
<body>
<h2>Temperature</h2>
<div class="block1">
<p>Measured Temperature: <span id='varMesT' >0 </span>&degC</p>
<p>Current Temperature: <span id='varCurrT'>0 </span>&degC</p>
</div>
<div class="block1">
********* Debug *********
<p>valComputePID - <span id='varComputePID'>0 </span>%</p>
<p>Mode: <span id='varMode'> </span><p>
<p>Status: <span id='varStatus'> </span><p>
*************************
</div>
    <!-------------------------JavaScrip------------------------->
    <script >
        setInterval(reload, 1000);  // time in milliseconds
        //------------------- Temperature -----------------------
        function reload()           // update the temperature every 1s
        {
            var xhr = new XMLHttpRequest();
            xhr.onreadystatechange = function()
            {
                if(this.readyState == 4 && this.status == 200)
                {
                  var obj = JSON.parse(this.responseText);
                  document.getElementById('varMesT').innerHTML = obj.varMesT;
                  document.getElementById('varCurrT').innerHTML = obj.varCurrT;
                }
            };
            xhr.open('GET', '/tempurl', true);
            xhr.send();
        }
        //--------------------- Status --------------------------
        setInterval(statusreload, 1000); // time in milliseconds
        function statusreload()          // update debug
        {
            var xhr = new XMLHttpRequest();
            xhr.onreadystatechange = function()
            {
                if(this.readyState == 4 && this.status == 200)
                {   // parse JSON text
                  var obj = JSON.parse(this.responseText);
                  document.getElementById('varMode').innerHTML = obj.varMode;
                  document.getElementById('varStatus').innerHTML = obj.varStatus;
                }
            };
            xhr.open('GET', '/statusurl', true);
            xhr.send();
        }
        //-------------------- debuger ------------------------
        setInterval(ComputePIDreload, 1000); // time in milliseconds
        function ComputePIDreload()          // update debug
        {
            var xhr = new XMLHttpRequest();
            xhr.onreadystatechange = function()
            {
                if(this.readyState == 4 && this.status == 200)
                {   // parse JSON text
                  var obj = JSON.parse(this.responseText);
                  document.getElementById('varComputePID').innerHTML = obj.varComputePID;
                }
            };
            xhr.open('GET', '/pidurl', true);
            xhr.send();
        }
        //-------------------------------------------------------
    </script>
</body>
</html>
)";