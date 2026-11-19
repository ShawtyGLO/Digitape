# Digitape

Instrument temps / hauteur / texture en C++ / JUCE : varispeed, delay et réverbe.

## L'idée

L'ADN d'un Space Echo avec le grain digital des réverbes typées des années 90, comme Lexicon 224, AMS RMX16, Publison, Eventide, etc.
Je voulais donner du grain digital à la réverbe, pouvoir m'amuser avec le delay et la réverbe comme on joue avec l'unité physique de Roland.

J'ai pu affiner l'idée que les limitations de la tape, comme la saturation, l'usure et autres, soient retranscrites en limitations digitales comme la quantification, le bitcrush, etc.

Le feedback est très intéressant et la possibilité de delay entre une mesure calée au tempo du projet ainsi que du temps libre rend les possibilités de sonorités très intéressantes et expérimentales.

## Validation

Prototype Python dans `_proto/` : bancs de mesure et rendus de comparaison avant le portage en
C++. Les sessions FL Studio de test sont versionnées avec le projet, pour que chaque comparaison
reparte du même matériel.

## Construire

Ouvrir `Digitape.jucer` dans Projucer, puis compiler sous Visual Studio 2022.
Cible : VST3, Windows 64 bits. Validation avec [pluginval](https://github.com/Tracktion/pluginval).

## Note

Conception et validation sont de moi. L'écriture du code s'appuie sur une assistance IA.

## Droits

© Lucas Lallier-Ginoux. **Tous droits réservés.** Dépôt publié en consultation uniquement,
comme portfolio. Aucune licence d'utilisation, de modification, de redistribution ou
d'exploitation commerciale n'est accordée.
