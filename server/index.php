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

$focus=-1;
if(isset($_POST['focus'])) {
  $focus=$_POST['focus'];
}

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
$rgb0=array(0,0,0);
$txx0=0;
$sel0=0;
$rgb1=array(0,0,0);
$txx1=0;
$sel1=0;
$rgb2=array(0,0,0);
$txx2=0;
$sel2=0;
$simday=60;
$simhour=12;

function parseColor($s) {
  if (preg_match('/^#([0-9a-f]{3})$/', $s)) {
    return array(
      hexdec(substr($s, 1, 1)) * 0x11,
      hexdec(substr($s, 2, 1)) * 0x11,
      hexdec(substr($s, 3, 1)) * 0x11,
    );
  } else {
    return array(
      hexdec(substr($s, 1, 2)),
      hexdec(substr($s, 3, 2)),
      hexdec(substr($s, 5, 2)),
    );
  }
}

$month_offset = array(0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334);
function get_daynum($month, $day) {
  global $month_offset;
  return $month_offset[$month-1] + $day;
}

function clamp8($f) {
  if ($f <= 0.0) return 0;
  else if ($f >= 255.0) return 255;
  $b = intval($f+0.5);
  if ($b <= 0) return 0;
  if ($b >= 255) return 255;
  return $b;
}

$cold = array( 0, 0, 255 );
$mid =  array( 0, 255, 0 );
$warm = array( 150, 40, 0 );
$hot =  array( 255, 0, 0 );

function interpolate($v, $lo, $hi) {
  return array(
    clamp8($lo[0]*(1.0-$v) + $hi[0]*$v),
    clamp8($lo[1]*(1.0-$v) + $hi[1]*$v),
    clamp8($lo[2]*(1.0-$v) + $hi[2]*$v));
}

function gradient($txx) {
  global $cold, $mid, $warm, $hot;
  if ($txx < -7.0)
    $txx = -7.0;
  else if ($txx > 7.0)
    $txx = 7.0;
  if ($txx < -2.0)
    return interpolate(($txx + 7.0) / 5.0, $cold, $mid);
  else if ($txx < 0.0)
    return interpolate(($txx + 2.0) / 2.0, $mid, $warm);
  else
    return interpolate(($txx + 0.0) / 5.0, $warm, $hot);
}

foreach ($lines as $line) {
  $v = explode(" ", $line);
  // echo("count: " . count($v) . " line: [" . $line . "]");
  // echo("<br>");
  if (count($v) == 2 && $v[0] == "mode:" && $v[1] == "auto") {
    $mode=0;
  } else if (count($v) == 5 && $v[0] == "mode:" && $v[1] == "manual") {
    $mode=1;
    if (substr($v[2], 0, 1) == '#') {
      $rgb0=parseColor($v[2]);
      $sel0=0;
    } else {
      $txx0=$v[2];
      $rgb0=gradient($txx0);
      $sel0=1;
    }
    if (substr($v[3], 0, 1) == '#') {
      $rgb1=parseColor($v[3]);
      $sel1=0;
    } else {
      $txx1=$v[3];
      $rgb1=gradient($txx1);
      $sel1=1;
    }
    if (substr($v[4], 0, 1) == '#') {
      $rgb2=parseColor($v[4]);
      $sel2=0;
    } else {
      $txx2=$v[4];
      $rgb2=gradient($txx2);
      $sel2=1;
    }
  } else if (count($v) == 4 && $v[0] == "mode:" && $v[1] == "simulate") {
    $mode=2;
    $mmdd = explode("/", $v[2]);
    $simday=get_daynum((int)$mmdd[0], (int)$mmdd[1]);
    $hhmm = explode(":", $v[3]);
    $simhour=(int)$hhmm[0];
  }
}

?>
</pre>
</font>

<script>

function scale(dir, step, lo, hi, id) {
  var slider = document.getElementById(id);
  var v = Number(slider.value);
  // console.log("current " + id + " slider value is " + v);
  v += dir * step;
  if (v < lo) v =  lo;
  else if (v > hi) v = hi;
  // console.log("udpated " + id + " slider value is " + v);
  slider.value = v;
  if ("createEvent" in document) {
    var evt = document.createEvent("HTMLEvents");
    evt.initEvent("change", false, true);
    slider.dispatchEvent(evt);
  } else {
    slider.fireEvent("onchange");
  }
}

function checkKey(e) {
  e = e || window.event;
  var focus = <?php echo($focus); ?>;
  if (focus < 0)
    return;
  var dir = 0;
  if (e.keyCode == '37') { // left arrow
    dir = -1;
  } else if (e.keyCode == '39') { // right arrow
    dir = 1;
  } else {
    return;
  }
  if (focus == 0) {
    scale(dir, 10, 0, 255, "R0");
  } else if (focus == 1) {
    scale(dir, 10, 0, 255, "G0");
  } else if (focus == 2) {
    scale(dir, 10, 0, 255, "B0");
  } else if (focus == 3) {
    scale(dir, 0.2, -7, 7, "TXX0");
  } else if (focus == 4) {
    scale(dir, 10, 0, 255, "R1");
  } else if (focus == 5) {
    scale(dir, 10, 0, 255, "G1");
  } else if (focus == 6) {
    scale(dir, 10, 0, 255, "B1");
  } else if (focus == 7) {
    scale(dir, 0.2, -7, 7, "TXX1");
  } else if (focus == 8) {
    scale(dir, 10, 0, 255, "R2");
  } else if (focus == 9) {
    scale(dir, 10, 0, 255, "G2");
  } else if (focus == 10) {
    scale(dir, 10, 0, 255, "B2");
  } else if (focus == 11) {
    scale(dir, 0.2, -7, 7, "TXX2");
  } else if (focus == 12) {
    scale(dir, 1, 0, 23, "SIMHOUR");
  } else if (focus == 13) {
    scale(dir, 1, 60, 181, "SIMDAY");
  }
}

document.onkeydown = checkKey;

function decToHex(d, padding) {
  var hex = Number(d).toString(16);
  while (hex.length < padding) {
    hex = "0" + hex;
  }
  return hex;
}

function encodeColor(t, sel) {
  if (sel == 0)
    return "#" +
      decToHex(document.getElementById("R"+t).value, 2) +
      decToHex(document.getElementById("G"+t).value, 2) +
      decToHex(document.getElementById("B"+t).value, 2);
  else
    return document.getElementById("TXX"+t).value;
}

function encodemode(m, reason) {
  if (m == 0) {
    return "auto";
  } else if (m == 1) {
    var sel0 = <?php echo($sel0); ?>;
    var sel1 = <?php echo($sel1); ?>;
    var sel2 = <?php echo($sel2); ?>;
    if (reason == 0)
      sel0 = 0;
    else if (reason == 1)
      sel0 = 1;
    else if (reason == 2)
      sel1 = 0;
    else if (reason == 3)
      sel1 = 1;
    else if (reason == 4)
      sel2 = 0;
    else if (reason == 5)
      sel2 = 1;
    return "manual " + 
      encodeColor(0, sel0) + " " +
      encodeColor(1, sel1) + " " +
      encodeColor(2, sel2);
  } else if (m == 2) {
    var month_offset = [ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 ];
    var hr = document.getElementById("SIMHOUR").value;
    var daynum = document.getElementById("SIMDAY").value;
    var month = 1;
    var day = Number(daynum);
    for (let m = 1; m < 12; m++) {
      if (daynum <= month_offset[m])
        break;
      month = m+1;
      day = daynum - month_offset[m];
    }
    return "simulate " + month + "/" + day + " " + hr + ":00";
  } else {
    return "auto";
  }
}

function setmode(focus, m, reason) {
  const form = document.createElement('form');
  form.method = 'POST';
  form.action = 'https://assembler.kwalsh.org/climate/index.php';

  const hiddenField = document.createElement('input');
  hiddenField.type = 'hidden';
  hiddenField.name = 'mode';
  hiddenField.value = "mode=" + encodemode(m, reason);
  form.appendChild(hiddenField);
  if (focus >= 0) {
    const hiddenField2 = document.createElement('input');
    hiddenField2.type = 'hidden';
    hiddenField2.name = 'focus';
    hiddenField2.value = "focus=" + focus;
    form.appendChild(hiddenField2);
  }
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
  <input type="radio" id="auto" name="mode" value="auto" onclick="setmode(-1,0,-1);" <?php if ($mode==0) echo "checked"; ?>>
  <label for="auto">auto</label><br>
  <input type="radio" id="manual" name="mode" value="manual" onclick="setmode(-1,1,-1);" <?php if ($mode==1) echo "checked"; ?>>
  <label for="css">manual</label><br>
  <input type="radio" id="simulate" name="mode" value="simulate" onclick="setmode(-1,2,-1);" <?php if ($mode==2) echo "checked"; ?>>
  <label for="css">simulate</label><br>
<p>
<div class="sliderdiv">
Manual controls:<br>
R0: <input type="range" min="0" max="255"           value="<?php echo($rgb0[0]); ?>" onchange="setmode(0,1,0);"  class="rslider" id="R0"><br>
G0: <input type="range" min="0" max="255"           value="<?php echo($rgb0[1]); ?>" onchange="setmode(1,1,0);"  class="gslider" id="G0"><br>
B0: <input type="range" min="0" max="255"           value="<?php echo($rgb0[2]); ?>" onchange="setmode(2,1,0);"  class="bslider" id="B0"><br>
X0: <input type="range" min="-7" max="7" step="0.1" value="<?php echo($txx0); ?>"    onchange="setmode(3,1,1);" class="tslider" id="TXX0"><br>
<br>
R1: <input type="range" min="0" max="255"           value="<?php echo($rgb1[0]); ?>" onchange="setmode(4,1,2);"  class="rslider" id="R1"><br>
G1: <input type="range" min="0" max="255"           value="<?php echo($rgb1[1]); ?>" onchange="setmode(5,1,2);"  class="gslider" id="G1"><br>
B1: <input type="range" min="0" max="255"           value="<?php echo($rgb1[2]); ?>" onchange="setmode(6,1,2);"  class="bslider" id="B1"><br>
X1: <input type="range" min="-7" max="7" step="0.1" value="<?php echo($txx1); ?>"    onchange="setmode(7,1,3);" class="tslider" id="TXX1"><br>
<br>
R2: <input type="range" min="0" max="255"           value="<?php echo($rgb2[0]); ?>" onchange="setmode(8,1,4);"  class="rslider" id="R2"><br>
G2: <input type="range" min="0" max="255"           value="<?php echo($rgb2[1]); ?>" onchange="setmode(9,1,4);"  class="gslider" id="G2"><br>
B2: <input type="range" min="0" max="255"           value="<?php echo($rgb2[2]); ?>" onchange="setmode(10,1,4);"  class="bslider" id="B2"><br>
X2: <input type="range" min="-7" max="7" step="0.1" value="<?php echo($txx2); ?>"    onchange="setmode(11,1,5);" class="tslider" id="TXX2"><br>
<br>
Simulation controls:<br>
Hour: <input type="range" min="0" max="23" value="<?php echo($simhour); ?>" onchange="setmode(12,2,-1);" class="tslider" id="SIMHOUR"><br>
Day: <input type="range" min="60" max="181" value="<?php echo($simday); ?>" onchange="setmode(13,2,-1);" class="tslider" id="SIMDAY"><br>
</div>
</form>

<!-- <br>
<br>
<button onclick="reset()">Reset Arduino</button> -->

</body></html>
