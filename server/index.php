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

if(isset($_POST['modes'])) {
  $modes=$_POST['modes'];
  $cURL = curl_init();
  $setopt_array = array(CURLOPT_URL => "http://assembler.kwalsh.org:8888",
	  CURLOPT_RETURNTRANSFER => true,
	  CURLOPT_USERPWD => "climate:vaporous-cardboard",
	  CURLOPT_POST => 1,
	  CURLOPT_POSTFIELDS => $modes,
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
$mode0=0;
$mode1=0;
$mode2=0;
$rgbenable0="";
$txxenable0="";
$rgbenable1="";
$txxenable1="";
$rgbenable2="";
$txxenable2="";
$rgb0=array(0,0,0);
$txx0=0;
$rgb1=array(0,0,0);
$txx1=0;
$rgb2=array(0,0,0);
$txx2=0;
foreach ($lines as $line) {
  $v = explode(" ", $line);
  // echo("count: " . count($v) . " line: [" . $line . "]");
  // echo("<br>");
  if (count($v) == 5 && $v[0] == "mode0:" && $v[1] == "manual") {
    $mode0=1;
    $rgb0=array_slice($v, 2);
  } else if (count($v) == 3 && $v[0] == "mode0:" && $v[1] == "gradient") {
    $mode0=2;
    $txx0=$v[2];
  } else if (count($v) == 2 && $v[0] == "mode0:" && $v[1] == "auto") {
    $mode0=0;
  } else if (count($v) == 5 && $v[0] == "mode1:" && $v[1] == "manual") {
    $mode1=1;
    $rgb1=array_slice($v, 2);
  } else if (count($v) == 3 && $v[0] == "mode1:" && $v[1] == "gradient") {
    $mode1=2;
    $txx1=$v[2];
  } else if (count($v) == 2 && $v[0] == "mode1:" && $v[1] == "auto") {
    $mode1=0;
  } else if (count($v) == 5 && $v[0] == "mode2:" && $v[1] == "manual") {
    $mode2=1;
    $rgb2=array_slice($v, 2);
  } else if (count($v) == 3 && $v[0] == "mode2:" && $v[1] == "gradient") {
    $mode2=2;
    $txx2=$v[2];
  } else if (count($v) == 2 && $v[0] == "mode2:" && $v[1] == "auto") {
    $mode2=0;
  }
}

?>
</pre>
</font>

<script>

function encodemode(i, m) {
  if (m == 0) {
    return "auto";
  } else if (m == 2) {
    return "gradient " + document.getElementById("TXX" + i).value;
  } else {
    return "manual " +
      document.getElementById("R"+i).value + " " +
      document.getElementById("G"+i).value + " " +
      document.getElementById("B"+i).value;
  }
}

function setmode(i, m) {
  const form = document.createElement('form');
  form.method = 'POST';
  form.action = 'https://assembler.kwalsh.org/climate/index.php';

  const hiddenField = document.createElement('input');
  hiddenField.type = 'hidden';
  hiddenField.name = 'modes';
  var mode0 = "<?php echo($mode0) ?>";
  var mode1 = "<?php echo($mode1) ?>";
  var mode2 = "<?php echo($mode2) ?>";
  if (i == 0) {
    mode0 = m;
  } else if (i == 1) {
    mode1 = m;
  } else if (i == 2) {
    mode2 = m;
  }
  hiddenField.value = "modes="
		  + encodemode(0, mode0) + "|"
		  + encodemode(1, mode1) + "|"
		  + encodemode(2, mode2);
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
  Mode for strip 0:<br>
  <input type="radio" id="auto0" name="mode0" value="auto" onclick="setmode(0, 0);" <?php if ($mode0==0) echo "checked"; ?>>
  <label for="auto">auto</label><br>
  <input type="radio" id="manual0" name="mode0" value="manual" onclick="setmode(0, 1);" <?php if ($mode0==1) echo "checked"; ?>>
  <label for="css">manual</label><br>
  <input type="radio" id="gradient0" name="mode0" value="gradient" onclick="setmode(0, 2);" <?php if ($mode0==2) echo "checked"; ?>>
  <label for="css">gradient</label><br>
<p>
<div class="sliderdiv">
R0: <input type="range" min="0" max="255" value="<?php echo($rgb0[0]); ?>" onchange="setmode(0,1);" class="rslider<?php echo($rgbenable0); ?>" id="R0" <?php echo($rgbenable0); ?>><br>
G0: <input type="range" min="0" max="255" value="<?php echo($rgb0[1]); ?>" onchange="setmode(0,1);" class="gslider<?php echo($rgbenable0); ?>" id="G0" <?php echo($rgbenable0); ?>><br>
B0: <input type="range" min="0" max="255" value="<?php echo($rgb0[2]); ?>" onchange="setmode(0,1);" class="bslider<?php echo($rgbenable0); ?>" id="B0" <?php echo($rgbenable0); ?>><br>
Gradient: <input type="range" min="-7" max="7" step="0.1" value="<?php echo($txx0); ?>" onchange="setmode(0,2);" class="tslider<?php echo($txxenable0); ?>" id="TXX0" <?php echo($txxenable0); ?>><br>
</div>

<p>
<form action="/control.php">
  Mode for strip 1:<br>
  <input type="radio" id="auto1" name="mode1" value="auto" onclick="setmode(1, 0);" <?php if ($mode1==0) echo "checked"; ?>>
  <label for="auto">auto</label><br>
  <input type="radio" id="manual1" name="mode1" value="manual" onclick="setmode(1, 1);" <?php if ($mode1==1) echo "checked"; ?>>
  <label for="css">manual</label><br>
  <input type="radio" id="gradient1" name="mode1" value="gradient" onclick="setmode(1, 2);" <?php if ($mode1==2) echo "checked"; ?>>
  <label for="css">gradient</label><br>
<p>
<div class="sliderdiv">
R1: <input type="range" min="0" max="255" value="<?php echo($rgb1[0]); ?>" onchange="setmode(1,1);" class="rslider<?php echo($rgbenable1); ?>" id="R1" <?php echo($rgbenable1); ?>><br>
G1: <input type="range" min="0" max="255" value="<?php echo($rgb1[1]); ?>" onchange="setmode(1,1);" class="gslider<?php echo($rgbenable1); ?>" id="G1" <?php echo($rgbenable1); ?>><br>
B1: <input type="range" min="0" max="255" value="<?php echo($rgb1[2]); ?>" onchange="setmode(1,1);" class="bslider<?php echo($rgbenable1); ?>" id="B1" <?php echo($rgbenable1); ?>><br>
Gradient: <input type="range" min="-7" max="7" step="0.1" step="0.1" value="<?php echo($txx1); ?>" onchange="setmode(1,2);" class="tslider<?php echo($txxenable1); ?>" id="TXX1" <?php echo($txxenable1); ?>><br>
</div>

<p>
<form action="/control.php">
  Mode for strip 2:<br>
  <input type="radio" id="auto2" name="mode2" value="auto" onclick="setmode(2, 0);" <?php if ($mode2==0) echo "checked"; ?>>
  <label for="auto">auto</label><br>
  <input type="radio" id="manual2" name="mode2" value="manual" onclick="setmode(2, 1);" <?php if ($mode2==1) echo "checked"; ?>>
  <label for="css">manual</label><br>
  <input type="radio" id="gradient2" name="mode2" value="gradient" onclick="setmode(2, 2);" <?php if ($mode2==2) echo "checked"; ?>>
  <label for="css">gradient</label><br>
<p>
<div class="sliderdiv">
R2: <input type="range" min="0" max="255" value="<?php echo($rgb2[0]); ?>" onchange="setmode(2,1);" class="rslider<?php echo($rgbenable2); ?>" id="R2" <?php echo($rgbenable2); ?>><br>
G2: <input type="range" min="0" max="255" value="<?php echo($rgb2[1]); ?>" onchange="setmode(2,1);" class="gslider<?php echo($rgbenable2); ?>" id="G2" <?php echo($rgbenable2); ?>><br>
B2: <input type="range" min="0" max="255" value="<?php echo($rgb2[2]); ?>" onchange="setmode(2,1);" class="bslider<?php echo($rgbenable2); ?>" id="B2" <?php echo($rgbenable2); ?>><br>
Gradient: <input type="range" min="-7" max="7>" value="<?php echo($txx2); ?>" onchange="setmode(2,2);" class="tslider<?php echo($txxenable2); ?>" id="TXX2" <?php echo($txxenable2); ?>><br>
</div>


<!-- <br>
<br>
<button onclick="reset()">Reset Arduino</button> -->

</body></html>
