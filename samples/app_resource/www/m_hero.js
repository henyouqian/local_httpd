jQuery(document).ready(function($) {
	$("#btn_serverpath").bind("touchend", function() {
		$.getJSON("/serverpath", function(json) {
            //alert(2);
			$("#path").text(json.path);
		});
	});
    var bodyStyle = $("body")[0].style;
    $("#colorpicker").minicolors({
        inline:true,
        change:function(hex, opacity){
            $.getJSON("/setbgcolor", $(this).minicolors("rgbObject"));
        }
    });
});
