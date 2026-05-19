  const FIREBASE_URL = "https://votre-projet-dans-firebase000.com";
  const FIREBASE_NOEUD = "/poubelles.json";

  // Intervalle de rafraîchissement en millisecondes
  const INTERVALLE = 3000;

  //   Couleur de la jauge selon le niveau
  function couleur(pct) {
    if (pct >= 90) return 'linear-gradient(90deg,#991b1b,#ef4444)';
    if (pct >= 75) return 'linear-gradient(90deg,#92400e,#f59e0b)';
    return 'linear-gradient(90deg,#14532d,#22c55e)';
  }

  //   Créer une carte html pour une poubelle
  function creerCarte(id) {
    const div = document.createElement('div');
    div.className = 'carte';
    div.id = 'carte-' + id;

    // Structure HTML de la carte
    div.innerHTML = `
      <div class="carte-titre">🗑️ <span id="nom-${id}">Chargement...</span></div>

      <div class="jauge-hdr">
        <span>Remplissage</span>
        <span id="pct-lbl-${id}" style="font-weight:700">0%</span>
      </div>
      <div class="jauge-track">
        <div class="jauge-fill" id="jauge-${id}" style="width:0%"></div>
      </div>

      <div class="stat">
        <span class="stat-lbl">Espace restant</span>
        <span class="stat-val" id="dist-${id}">--</span>
      </div>
      <div class="stat">
        <span class="stat-lbl">Remplissage</span>
        <span class="stat-val" id="rem-${id}">--</span>
      </div>
      <div class="stat">
        <span class="stat-lbl">État</span>
        <span class="stat-val"><span class="badge badge-dispo" id="badge-${id}">--</span></span>
      </div>
    `;

    document.getElementById('grille').appendChild(div);
  }

  //   Mettre à jour une carte avec les nouvelles données
  function mettreAJourCarte(id, data) {
    // Créer la carte si elle n'existe pas encore
    if (!document.getElementById('carte-' + id)) {
      creerCarte(id);
    }

    const pct  = parseFloat(data.remplissage || 0);
    const dist = parseFloat(data.distance    || 0);
    const etat = data.etat || '--';
    const nom  = data.nom  || id;

    // Nom de la poubelle
    document.getElementById('nom-'    + id).textContent = nom;

    // Jauge
    document.getElementById('jauge-'  + id).style.width      = pct + '%';
    document.getElementById('jauge-'  + id).style.background = couleur(pct);
    document.getElementById('pct-lbl-'+ id).textContent      = pct.toFixed(0) + '%';

    // Stats
    document.getElementById('dist-'+ id).textContent = dist.toFixed(1) + ' cm';
    document.getElementById('rem-' + id).textContent = pct.toFixed(1) + '%';

    // Badge état
    const badge = document.getElementById('badge-' + id);
    badge.textContent = etat;
    badge.className   = 'badge';
    if      (pct >= 90) badge.classList.add('badge-plein');
    else if (pct >= 75) badge.classList.add('badge-att');
    else                badge.classList.add('badge-dispo');

    // Classe de la carte (bordure colorée)
    const carte = document.getElementById('carte-' + id);
    carte.className = 'carte';
    if      (pct >= 90) carte.classList.add('pleine');
    else if (pct >= 75) carte.classList.add('attention');
    else                carte.classList.add('dispo');

    return pct >= 90;  // qui retourne true si pleine
  }

  //   Lecture firebase et mise à jour de toutes les cartes
  function rafraichir() {
    const url = FIREBASE_URL + FIREBASE_NOEUD;

    fetch(url)
      .then(response => {
        // Vérifier que la réponse est OK (code 200)
        if (!response.ok) throw new Error('Erreur Firebase : ' + response.status);
        return response.json();  // convertir la réponse en objet JavaScript
      })
      .then(data => {

        // Indicateur de connexion : vert = OK
        document.getElementById('dot').style.background = '#22c55e';

        if (!data) {
          console.log('Firebase vide — aucune donnée encore');
          return;
        }

        let uneEstPleine = false;

        // Parcourir chaque poubelle dans Firebase
        // Object.entries() transforme l'objet en tableau [clé, valeur]
        Object.entries(data).forEach(([id, poubelle]) => {
          const estPleine = mettreAJourCarte(id, poubelle);
          if (estPleine) uneEstPleine = true;
        });

        // Alerte globale + son si au moins une poubelle est pleine
        const alerte = document.getElementById('alerte-globale');
        if (uneEstPleine) {
          alerte.style.display = 'block';
        } else {
          alerte.style.display = 'none';
        }
      })
      .catch(err => {
        // Erreur de connexion : indicateur rouge
        document.getElementById('dot').style.background = '#ef4444';
        console.error('Erreur lecture Firebase :', err);
      });
  }

  // Lancer immédiatement puis répéter toutes les 3 secondes
  rafraichir();
  setInterval(rafraichir, INTERVALLE);