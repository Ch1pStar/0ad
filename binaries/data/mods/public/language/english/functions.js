function number(n, object) {
  var word = lookup("nouns", object);

  if (! word) // the word couldn't be found in the dictionary
    return n+" "+object;

  if (n == 1)
    return n+" "+word.singular;
  else
    return n+" "+word.plural;
}

/*  var r="";
  while (1) {
    r = (n%1000)+r;
    n = Math.floor(n/1000);
    if (n) {
      r = ","+r;
    } else {
      break;
    }
  }
  return r;*/

function test() {
  return "banana2";
}