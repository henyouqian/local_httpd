jQuery(document).ready(function($) {
	$("#btn_serverpath").bind("click", function() {
		$.getJSON("/serverpath", function(json) {
            //alert(2);
			$("#path").text(json.path);
		});
	});
	var bodyStyle = $("body")[0].style;
	$("#colorpicker").minicolors({
        inline:true,
        change:function(hex, opacity){
            bodyStyle.backgroundColor = hex;
        }
    });
});
