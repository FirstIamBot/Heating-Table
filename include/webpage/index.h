/*******************************************************************************
 * Sketch name: AJAX code with JSON parsing
 * Description: Webpage code with JSON for revised Listing 8-1
 * Created on:  October 2020
 * Author:      Neil Cameron
 * Book:        Electronics Projects with the ESP8266 and ESP32
 * Chapter :    8 - Updating a webpage
 ******************************************************************************/

char page_index[] PROGMEM = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <!----------------------------CSS---------------------------->
      <style>
    <style>
        body {background-color: rgba(128, 128, 128, 0.884)}
        h4 {font-family: arial; text-align: center; color: white;}
        .card
        {
            max-width: 450px;
            min-height: 100px;
            background: rgba(213, 174, 174, 0.52);
            padding: 10px;
            font-weight: bold;
            font: 25px calibri;
            text-align: center;
            box-sizing: border-box;
            color: blue;
            margin:20px;
            box-shadow: 0px 2px 15px 15px rgba(0,0,0,0.75);
        }
    </style>
    <!----------------------------HTML--------------------------->
    <title>Heat Table ESP32</title>
</head>
<body>
<div class="card">
    <h4 id='varMode'></h4>
    <p>Measured Temperature: <span id='varMesT' >0 </span>&degC</p>
    <p>Current Temperature: <span id='varCurrT'>0 </span>&degC</p>
</div>
    <!-------------------------JavaScrip------------------------->
    <script >
        setInterval(reload, 1000);  // time in milliseconds
        //-------------------------------------------------------
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
        //-------------------------------------------------------
    </script>
</body>
</html>
)";