jQuery(document).ready(function($) {
	$("#btn_serverpath").bind("touchend", function() {
		$.getJSON("/serverpath", function(json) {
            //alert(2);
			$("#path").text(json.path);
		});
	});
});
