<html><head><title>Climate Sculpture</title>
<style>
input[type="range"] {
 -webkit-appearance: none;
 width: 100%;
}

input[type="range"]:focus {
 outline: none;
}
input[type="range"]::-webkit-slider-runnable-track {
 height: 5px;
}
input[type="range"]::-moz-range-track {
 height: 5px;
}
.rslider::-webkit-slider-runnable-track { background: pink; }
.rslider::-moz-range-track { background: pink; }
.gslider::-webkit-slider-runnable-track { background: lightgreen; }
.gslider::-moz-range-track { background: lightgreen; }
.bslider::-webkit-slider-runnable-track { background: skyblue; }
.bslider::-moz-range-track { background: skyblue; }
.tslider::-webkit-slider-runnable-track { background: gray; }
.tslider::-moz-range-track { background: gray; }
.rsliderdisabled::-webkit-slider-runnable-track { background: pink; }
.rsliderdisabled::-moz-range-track { background: pink; }
.gsliderdisabled::-webkit-slider-runnable-track { background: lightgreen; }
.gsliderdisabled::-moz-range-track { background: lightgreen; }
.bsliderdisabled::-webkit-slider-runnable-track { background: skyblue; }
.bsliderdisabled::-moz-range-track { background: skyblue; }
.tsliderdisabled::-webkit-slider-runnable-track { background: gray; }
.tsliderdisabled::-moz-range-track { background: gray; }
input[type="range"]::-webkit-slider-thumb {
 -webkit-appearance: none;
 height: 15px;
 width: 15px;
 margin-top: -5px;
 border-radius: 50%;
}
input[type="range"]::-moz-range-thumb {
 height: 15px;
 width: 15px;
 margin-top: -5px;
 border-radius: 50%;
}
.rslider::-webkit-slider-thumb { background: red; }
.rslider::-moz-range-thumb { background: red; }
.gslider::-webkit-slider-thumb { background: green; }
.gslider::-moz-range-thumb { background: green; }
.bslider::-webkit-slider-thumb { background: blue; }
.bslider::-moz-range-thumb { background: blue; }
.tslider::-webkit-slider-thumb { background: black; }
.tslider::-moz-range-thumb { background: black; }
.rsliderdisabled::-webkit-slider-thumb { background: pink; }
.rsliderdisabled::-moz-range-thumb { background: pink; }
.gsliderdisabled::-webkit-slider-thumb { background: lightgreen; }
.gsliderdisabled::-moz-range-thumb { background: lightgreen; }
.bsliderdisabled::-webkit-slider-thumb { background: skyblue; }
.bsliderdisabled::-moz-range-thumb { background: skyblue; }
.tsliderdisabled::-webkit-slider-thumb { background: black; }
.tsliderdisabled::-moz-range-thumb { background: black; }
.sliderdiv {
  max-width: 600;
  width: 90%;
}
</style>
</head><body>

<h1>Climate Sculpture</h1>

<a href="http://assembler.kwalsh.org:8888/status">Current Status</a>
<br>
<a href="http://assembler.kwalsh.org:8888/monitor">Monitor</a>
<p>

<?php
set_time_limit(30);
$date = date('m/d/Y h:i:s a', time());
echo "$date\n";
echo "<br>\n";
?>

<font color="blue">
<pre>
<?php

if(isset($_POST['mode'])) {
  $mode=$_POST['mode'];
  $cURL = curl_init();
  $setopt_array = array(CURLOPT_URL => "http://assembler.kwalsh.org:8888",
	  CURLOPT_RETURNTRANSFER => true,
	  CURLOPT_USERPWD => "climate:vaporous-cardboard",
	  CURLOPT_POST => 1,
	  CURLOPT_POSTFIELDS => $mode,
	  CURLOPT_HTTPHEADER => array());
  curl_setopt_array($cURL, $setopt_array);
  $response_data = curl_exec($cURL);
  print_r($response_data);
  curl_close($cURL);
} else {
  $cURL = curl_init();
  $setopt_array = array(CURLOPT_URL => "http://assembler.kwalsh.org:8888",
	  CURLOPT_RETURNTRANSFER => true,
	  CURLOPT_HTTPHEADER => array());
  curl_setopt_array($cURL, $setopt_array);
  $response_data = curl_exec($cURL);
  print_r($response_data);
  curl_close($cURL);
}

$lines = explode("\n", str_replace("\r\n","\n", $response_data));
$mode=0;
// $rgbenable="disabled";
$rgbenable="";
$txxenable="";
$rgb=array(0,0,0,0,0,0,0,0,0);
$txx=0;
foreach ($lines as $line) {
  $v = explode(" ", $line);
  // echo("count: " . count($v) . " line: [" . $line . "]");
  // echo("<br>");
  if (count($v) == 11 && $v[0] == "mode:" && $v[1] == "manual") {
    $mode=1;
    $rgb=array_slice($v, 2);
    // $rgbenable="";
  } else if (count($v) == 3 && $v[0] == "mode:" && $v[1] == "gradient") {
    $mode=2;
    $txx=$v[2];
    // $rgbenable="";
  } else if (count($v) == 2 && $v[0] == "mode:" && $v[1] == "auto") {
    $mode=0;
    // $rgbenable="disabled";
  }
}

?>
</pre>
</font>

<script>
function setmode(m) {
  const form = document.createElement('form');
  form.method = 'POST';
  form.action = 'https://assembler.kwalsh.org/climate/index.php';

  const hiddenField = document.createElement('input');
  hiddenField.type = 'hidden';
  hiddenField.name = 'mode';
  if (m == 0) {
    hiddenField.value = "mode=auto";
  } else if (m == 2) {
    hiddenField.value = "mode=gradient " + document.getElementById("TXX").value;
  } else {
    hiddenField.value = "mode=manual " +
      document.getElementById("R0").value + " " +
      document.getElementById("G0").value + " " +
      document.getElementById("B0").value + " " +
      document.getElementById("R1").value + " " +
      document.getElementById("G1").value + " " +
      document.getElementById("B1").value + " " +
      document.getElementById("R2").value + " " +
      document.getElementById("G2").value + " " +
      document.getElementById("B2").value;
  }
  form.appendChild(hiddenField);
  document.body.appendChild(form);
  form.submit();
}

function reset(m) {
  const form = document.createElement('form');
  form.method = 'POST';
  form.action = 'https://assembler.kwalsh.org/climate/index.php';
  const hiddenField = document.createElement('input');
  hiddenField.type = 'hidden';
  hiddenField.name = 'reset';
  hiddenField.value = "reset";
  form.appendChild(hiddenField);
  document.body.appendChild(form);
  form.submit();
}
</script>

<p>
<form action="/control.php">
  Mode:<br>
  <input type="radio" id="auto" name="mode" value="auto" onclick="setmode(0);" <?php if ($mode==0) echo "checked"; ?>>
  <label for="auto">auto</label><br>
  <input type="radio" id="manual" name="mode" value="manual" onclick="setmode(1);" <?php if ($mode==1) echo "checked"; ?>>
  <label for="css">manual</label><br>
  <input type="radio" id="gradient" name="mode" value="gradient" onclick="setmode(2);" <?php if ($mode==2) echo "checked"; ?>>
  <label for="css">gradient</label><br>
<p>

<div class="sliderdiv">
R0: <input type="range" min="0" max="255" value="<?php echo($rgb[0]); ?>" onchange="setmode(1);" class="rslider<?php echo($rgbenable); ?>" id="R0" <?php echo($rgbenable); ?>><br>
G0: <input type="range" min="0" max="255" value="<?php echo($rgb[1]); ?>" onchange="setmode(1);" class="gslider<?php echo($rgbenable); ?>" id="G0" <?php echo($rgbenable); ?>><br>
B0: <input type="range" min="0" max="255" value="<?php echo($rgb[2]); ?>" onchange="setmode(1);" class="bslider<?php echo($rgbenable); ?>" id="B0" <?php echo($rgbenable); ?>><br>
<br>
R1: <input type="range" min="0" max="255" value="<?php echo($rgb[3]); ?>" onchange="setmode(1);" class="rslider<?php echo($rgbenable); ?>" id="R1" <?php echo($rgbenable); ?>><br>
G1: <input type="range" min="0" max="255" value="<?php echo($rgb[4]); ?>" onchange="setmode(1);" class="gslider<?php echo($rgbenable); ?>" id="G1" <?php echo($rgbenable); ?>><br>
B1: <input type="range" min="0" max="255" value="<?php echo($rgb[5]); ?>" onchange="setmode(1);" class="bslider<?php echo($rgbenable); ?>" id="B1" <?php echo($rgbenable); ?>><br>
<br>
R2: <input type="range" min="0" max="255" value="<?php echo($rgb[6]); ?>" onchange="setmode(1);" class="rslider<?php echo($rgbenable); ?>" id="R2" <?php echo($rgbenable); ?>><br>
G2: <input type="range" min="0" max="255" value="<?php echo($rgb[7]); ?>" onchange="setmode(1);" class="gslider<?php echo($rgbenable); ?>" id="G2" <?php echo($rgbenable); ?>><br>
B2: <input type="range" min="0" max="255" value="<?php echo($rgb[8]); ?>" onchange="setmode(1);" class="bslider<?php echo($rgbenable); ?>" id="B2" <?php echo($rgbenable); ?>><br>
<br>
<br>
Gradient: <input type="range" min="-15" max="25" value="<?php echo($txx); ?>" onchange="setmode(2);" class="tslider<?php echo($txxenable); ?>" id="TXX" <?php echo($txxenable); ?>><br>
</div>

<!-- <br>
<br>
<button onclick="reset()">Reset Arduino</button> -->

</body></html>
