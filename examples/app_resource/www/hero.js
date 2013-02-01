jQuery(document).ready(function($) {
	$("#btn_serverpath").bind("click", function() {
		$.getJSON("/serverpath", function(json) {
            //alert(2);
			$("#path").text(json.path);
		});
	});
	var bodyStyle = $("body")[0].style;
	$("#cp").colorpicker().on("changeColor", function(ev){
		bodyStyle.backgroundColor = ev.color.toHex();
	});
});
