// FragDesk Handy-App – ein Match (Platzhalter; wird ausgebaut)
import {h, emptyState} from '../ui.js';

export async function render(ctx) {
  ctx.title('Match');
  ctx.main.replaceChildren(emptyState({title: 'Match-Details kommen gleich', text: 'Dieser Bereich wird gerade gebaut.'}, 'list'));
}
