// URL da plataforma do Championship/juiz (Hostinger). Deixe vazio para esconder a ligação.
window.IPX_JUDGE_SITE = "";
(function () {
  var u = window.IPX_JUDGE_SITE;
  if (!u || !/^https:\/\//i.test(u)) return;
  document.querySelectorAll("header nav").forEach(function (n) {
    var a = document.createElement("a");
    a.href = u; a.textContent = "Championship";
    n.appendChild(a);
  });
})();
