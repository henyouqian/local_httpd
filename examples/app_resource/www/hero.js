jQuery(document).ready(function($) {
	$("#btn_serverpath").click(function(){
		$.getJSON("/serverpath", function(json) {
			$("#path").text(json.path);
		});
	});
});
