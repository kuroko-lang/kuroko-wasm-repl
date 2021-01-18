define("ace/theme/sunsmoke",["require","exports","module","ace/lib/dom"],function(e,t,n){
    t.isDark = true,
    t.cssClass = "ace-sunsmoke",
    t.cssText = "\
        body { background-color: #1f1f1f; } \
        .ace_editor .ace_marker-layer .ace_bracket { display: none }\
        .ace_editor  { color: #e6e6e6; background-color: #1f1f1f; }\
        .ace_gutter  { color: #968b39; background-color: #000000; }\
        .ace_keyword { color: #33a2e6; }\
        .ace_string  { color: #48b048; }\
        .ace_comment { color: #9e9981; font-style: oblique; }\
        .ace_numeric { color: #e62b7f; }\
        .ace_escape  { color: #71cbad; }\
        .ace_constant.ace_language.ace_escape  { color: #71cbad; }\
        .ace_storage { color: #e6ce6e; }\
        .ace_support.ace_function { color: #e6ce6e; }\
        .ace_selection { background-color: #e6ce6e; color: #1f1f1f; }\
        .ace_constant.ace_language { color: #e62b7f; }\
    ";
    var r=e("../lib/dom");
    r.importCssString(t.cssText,t.cssClass)
});

(function() {
    window.require(["ace/theme/sunsmoke"], function(m) {
        if (typeof module == "object" && typeof exports == "object" && module) {
            module.exports = m;
        }
    });
})();
