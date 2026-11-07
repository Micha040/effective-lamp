// FragDesk Handy-App – Statistiken (Platzhalter; wird ausgebaut)
import {h, emptyState} from '../ui.js';

export async function render(ctx) {
  ctx.title('Statistiken');
  ctx.main.replaceChildren(h('h1', {class: 'page-title'}, 'Statistiken'),
    emptyState({title: 'Statistiken kommen gleich', text: 'Dieser Bereich wird gerade gebaut.'}, 'stats'));
}
