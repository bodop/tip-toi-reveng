function play(oid) {
    /* Tested with firefox 27.0 */
    var xmlhttp=new XMLHttpRequest();
    xmlhttp.open("POST","http://localhost:9980/"+oid,true);
    xmlhttp.send();
}
