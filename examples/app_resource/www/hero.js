jQuery(document).ready(function($) {
	$("#btn_serverpath").bind("click", function() {
		$.getJSON("/serverpath", function(json) {
            //alert(2);
			$("#path").text(json.path);
		});
	});
});
